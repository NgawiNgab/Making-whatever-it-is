#pragma once
// Minimal, strict JSON reader/writer for scenario files and result reports.
// No external dependency: the simulation must remain bootstrap-reproducible.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mwi::json {

struct Value;

// An object stores shared pointers so this header can refer to Value while it is
// still being defined. Scenario files are tiny, so lookup cost is irrelevant.
using Array = std::vector<Value>;
using Object = std::map<std::string, std::shared_ptr<Value>>;

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    Array array;
    Object object;

    static Value null();
    static Value makeBool(bool value);
    static Value makeNumber(double value);
    static Value makeString(std::string value);
    static Value makeArray(Array value);
    static Value makeObject(Object value);

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    const char* typeName() const;

    const Value* get(std::string_view key) const;
    const Value& require(std::string_view key) const;
    std::string asString() const;
    double asDouble() const;
    int asInt() const;
    bool asBool() const;
    const Array& asArray() const;
    const Object& asObject() const;
};

class ParseError : public std::runtime_error {
public:
    ParseError(std::string message, std::size_t position);
    std::size_t position() const { return position_; }

private:
    std::size_t position_;
};

Value parse(std::string_view text);
std::string stringify(const Value& value, bool pretty = false);
std::string quote(std::string_view text);

// Convenience builders used by the result reporter.
Value object(std::initializer_list<std::pair<std::string, Value>> members);
Value array(std::initializer_list<Value> elements);

}  // namespace mwi::json
