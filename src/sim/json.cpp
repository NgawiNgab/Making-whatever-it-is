#include "sim/json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace mwi::json {
namespace {

constexpr int kMaxDepth = 64;

void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        throw std::runtime_error("Unicode codepoint out of range");
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value run() {
        skipWhitespace();
        Value result = parseValue(0);
        skipWhitespace();
        if (pos_ != text_.size()) fail("trailing content after JSON value");
        return result;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        // Every parse failure names the byte, so a caller can point at the text.
        if (message.find(" at byte ") != std::string::npos) throw ParseError(message, pos_);
        throw ParseError(message + " at byte " + std::to_string(pos_), pos_);
    }

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return eof() ? '\0' : text_[pos_]; }

    void skipWhitespace() {
        while (!eof()) {
            const char c = peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    void expect(char c) {
        if (eof() || peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return false;
        pos_ += word.size();
        return true;
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) fail("JSON nesting is too deep");
        skipWhitespace();
        if (eof()) fail("unexpected end of JSON");
        switch (peek()) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': return Value::makeString(parseString());
            case 't':
                if (!literal("true")) fail("invalid literal");
                return Value::makeBool(true);
            case 'f':
                if (!literal("false")) fail("invalid literal");
                return Value::makeBool(false);
            case 'n':
                if (!literal("null")) fail("invalid literal");
                return Value::null();
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parseNumber();
                fail("unexpected character");
        }
    }

    Value parseObject(int depth) {
        expect('{');
        Object members;
        skipWhitespace();
        if (peek() == '}') { ++pos_; return Value::makeObject(std::move(members)); }
        while (true) {
            skipWhitespace();
            if (peek() != '"') fail("expected object key string");
            std::string key = parseString();
            if (members.contains(key)) fail("duplicate object key: " + key);
            skipWhitespace();
            expect(':');
            Value child = parseValue(depth + 1);
            members.emplace(std::move(key), std::make_shared<Value>(std::move(child)));
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            fail("expected ',' or '}' in object");
        }
        return Value::makeObject(std::move(members));
    }

    Value parseArray(int depth) {
        expect('[');
        Array elements;
        skipWhitespace();
        if (peek() == ']') { ++pos_; return Value::makeArray(std::move(elements)); }
        while (true) {
            elements.push_back(parseValue(depth + 1));
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            fail("expected ',' or ']' in array");
        }
        return Value::makeArray(std::move(elements));
    }

    std::uint32_t parseHex4() {
        if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hexDigit(text_[pos_++]);
            if (digit < 0) fail("invalid hex digit in \\u escape");
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    // Validates one raw UTF-8 sequence starting at pos_ and returns its bytes.
    // Rejects continuation-byte starts, overlong forms, surrogates, and anything
    // above U+10FFFF, so a scenario file cannot smuggle invalid text into the report.
    std::string parseUtf8() {
        const std::size_t start = pos_;
        const auto first = static_cast<unsigned char>(text_[pos_]);
        std::size_t extra = 0;
        std::uint32_t code = 0;
        std::uint32_t minimum = 0;
        if ((first & 0xE0) == 0xC0) { extra = 1; code = first & 0x1Fu; minimum = 0x80; }
        else if ((first & 0xF0) == 0xE0) { extra = 2; code = first & 0x0Fu; minimum = 0x800; }
        else if ((first & 0xF8) == 0xF0) { extra = 3; code = first & 0x07u; minimum = 0x10000; }
        else fail("invalid UTF-8 leading byte");
        for (std::size_t i = 1; i <= extra; ++i) {
            if (pos_ + i >= text_.size()) fail("truncated UTF-8 sequence");
            const auto next = static_cast<unsigned char>(text_[pos_ + i]);
            if ((next & 0xC0) != 0x80) fail("invalid UTF-8 continuation byte");
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < minimum) fail("overlong UTF-8 encoding");
        if (code >= 0xD800 && code <= 0xDFFF) fail("UTF-8 surrogate is not allowed");
        if (code > 0x10FFFF) fail("UTF-8 code point out of range");
        pos_ += extra + 1;
        return std::string(text_.substr(start, extra + 1));
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (!eof()) {
            const unsigned char c = static_cast<unsigned char>(peek());
            if (c == '"') { ++pos_; return out; }
            if (c < 0x20) fail("unescaped control character in string");
            if (c >= 0x80) {  // multi-byte UTF-8: validate, never copy blind
                out += parseUtf8();
                continue;
            }
            if (c != '\\') { out.push_back(static_cast<char>(c)); ++pos_; continue; }
            ++pos_;
            if (eof()) fail("truncated string escape");
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code = parseHex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_.substr(pos_, 2) != "\\u")
                            fail("high surrogate is not followed by low surrogate");
                        pos_ += 2;
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        fail("unexpected low surrogate");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: fail("unknown string escape");
            }
        }
        fail("unterminated string");
    }

    Value parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (eof()) fail("truncated number");
        if (peek() == '0') {
            ++pos_;
            if (!eof() && peek() >= '0' && peek() <= '9') fail("leading zero in number");
        } else if (peek() >= '1' && peek() <= '9') {
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            fail("invalid number");
        }
        if (!eof() && peek() == '.') {
            ++pos_;
            if (eof() || peek() < '0' || peek() > '9') fail("decimal point without digits");
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            if (eof() || peek() < '0' || peek() > '9') fail("exponent without digits");
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string_view token = text_.substr(start, pos_ - start);
        double value = 0.0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value,
                                            std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size() ||
            !std::isfinite(value)) {
            fail("invalid or non-finite number");
        }
        return Value::makeNumber(value);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

void indent(std::string& out, int level) { out.append(static_cast<std::size_t>(level) * 2, ' '); }

std::string numberText(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream stream;
    stream << std::setprecision(15) << value;
    return stream.str();
}

void stringifyInto(std::string& out, const Value& value, bool pretty, int level) {
    switch (value.type) {
        case Value::Type::Null: out += "null"; return;
        case Value::Type::Bool: out += value.boolean ? "true" : "false"; return;
        case Value::Type::Number: out += numberText(value.number); return;
        case Value::Type::String: out += quote(value.string); return;
        case Value::Type::Array: {
            out += '[';
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                if (i) out += ',';
                if (pretty) { out += '\n'; indent(out, level + 1); }
                stringifyInto(out, value.array[i], pretty, level + 1);
            }
            if (pretty && !value.array.empty()) { out += '\n'; indent(out, level); }
            out += ']';
            return;
        }
        case Value::Type::Object: {
            out += '{';
            std::size_t index = 0;
            for (const auto& [key, child] : value.object) {
                if (index++) out += ',';
                if (pretty) { out += '\n'; indent(out, level + 1); }
                out += quote(key);
                out += ':';
                if (pretty) out += ' ';
                stringifyInto(out, child ? *child : Value::null(), pretty, level + 1);
            }
            if (pretty && !value.object.empty()) { out += '\n'; indent(out, level); }
            out += '}';
            return;
        }
    }
}

}  // namespace

ParseError::ParseError(std::string message, std::size_t position)
    : std::runtime_error(std::move(message)), position_(position) {}

Value Value::null() { return {}; }
Value Value::makeBool(bool value) { Value v; v.type = Type::Bool; v.boolean = value; return v; }
Value Value::makeNumber(double value) { Value v; v.type = Type::Number; v.number = value; return v; }
Value Value::makeString(std::string value) { Value v; v.type = Type::String; v.string = std::move(value); return v; }
Value Value::makeArray(Array value) { Value v; v.type = Type::Array; v.array = std::move(value); return v; }
Value Value::makeObject(Object value) { Value v; v.type = Type::Object; v.object = std::move(value); return v; }

const char* Value::typeName() const {
    switch (type) {
        case Type::Null: return "null";
        case Type::Bool: return "boolean";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Object: return "object";
    }
    return "unknown";
}

const Value* Value::get(std::string_view key) const {
    if (!isObject()) return nullptr;
    auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : it->second.get();
}

const Value& Value::require(std::string_view key) const {
    const Value* value = get(key);
    if (!value) throw std::runtime_error("missing required key: " + std::string(key));
    return *value;
}

std::string Value::asString() const {
    if (!isString()) throw std::runtime_error("expected string, got " + std::string(typeName()));
    return string;
}
double Value::asDouble() const {
    if (!isNumber()) throw std::runtime_error("expected number, got " + std::string(typeName()));
    return number;
}
int Value::asInt() const {
    const double value = asDouble();
    if (!std::isfinite(value) || std::floor(value) != value ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("expected a 32-bit integer");
    }
    return static_cast<int>(value);
}
bool Value::asBool() const {
    if (!isBool()) throw std::runtime_error("expected boolean, got " + std::string(typeName()));
    return boolean;
}
const Array& Value::asArray() const {
    if (!isArray()) throw std::runtime_error("expected array, got " + std::string(typeName()));
    return array;
}
const Object& Value::asObject() const {
    if (!isObject()) throw std::runtime_error("expected object, got " + std::string(typeName()));
    return object;
}

Value parse(std::string_view text) { return Parser(text).run(); }

std::string quote(std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string stringify(const Value& value, bool pretty) {
    std::string out;
    stringifyInto(out, value, pretty, 0);
    return out;
}

Value object(std::initializer_list<std::pair<std::string, Value>> members) {
    Object result;
    for (const auto& [key, value] : members) {
        result[key] = std::make_shared<Value>(value);
    }
    return Value::makeObject(std::move(result));
}

Value array(std::initializer_list<Value> elements) {
    return Value::makeArray(Array(elements));
}

}  // namespace mwi::json
