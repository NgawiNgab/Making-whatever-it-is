#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sim/json.h"
#include "sim/runner.h"
#include "sim/scenario.h"

namespace {

using mwi::json::Value;
using mwi::sim::Scenario;

struct Options {
    std::string command;
    std::string scenarioPath;
    std::string outputPath;
    bool compact = false;
};

std::string readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open file: " + path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad()) throw std::runtime_error("failed while reading file: " + path);
    return contents.str();
}

void writeFile(const std::string& path, std::string_view contents) {
    // Create parent directories: a report path such as out/latest/result.json is a
    // normal thing to ask for, and failing on it is a papercut, not a safety rule.
    if (const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec && !std::filesystem::is_directory(parent)) {
            throw std::runtime_error("cannot create output directory: " + parent.string() +
                                     " (" + ec.message() + ")");
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot write file: " + path);
    stream << contents;
    if (!stream) throw std::runtime_error("failed while writing file: " + path);
}

Value errorValue(std::string_view kind, const std::string& message, std::size_t position = 0) {
    return mwi::json::object({
        {"error", Value::makeString(message)},
        {"kind", Value::makeString(std::string(kind))},
        {"position", Value::makeNumber(static_cast<double>(position))},
    });
}

int printError(std::string_view kind, const std::string& message, std::size_t position = 0,
               int exitCode = 2) {
    std::cerr << mwi::json::stringify(errorValue(kind, message, position), true) << '\n';
    return exitCode;
}

void printHelp() {
    std::cout << R"(baritone-sim - headless autonomous-mining engine

Usage:
  baritone-sim version
  baritone-sim validate <scenario.json>
  baritone-sim run <scenario.json> [--output result.json] [--compact]

Commands:
  version   Print engine/schema/license metadata.
  validate  Parse and validate a scenario without running the bot.
  run       Execute a bounded simulation and print/write a JSON result.

Options:
  --output <path>   Write the result JSON to a file instead of stdout.
  --compact         Emit single-line JSON (default is pretty-printed).

Exit codes: 0 success, 2 usage/parse/validation/file error, 3 runtime error.
)";
}

Options parseOptions(int argc, char** argv) {
    if (argc < 2) throw std::runtime_error("missing command; try --help");
    Options options;
    options.command = argv[1];
    if (options.command == "--help" || options.command == "-h" || options.command == "help") {
        options.command = "help";
        return options;
    }
    if (options.command != "version" && options.command != "validate" && options.command != "run") {
        throw std::runtime_error("unknown command: " + options.command);
    }
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--compact") {
            options.compact = true;
        } else if (arg == "--output") {
            if (i + 1 >= argc) throw std::runtime_error("--output requires a path");
            options.outputPath = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error("unknown option: " + arg);
        } else if (options.scenarioPath.empty()) {
            options.scenarioPath = arg;
        } else {
            throw std::runtime_error("unexpected extra argument: " + arg);
        }
    }
    if (options.command == "version" && !options.scenarioPath.empty()) {
        throw std::runtime_error("version does not take a scenario path");
    }
    if (options.command != "version" && options.scenarioPath.empty()) {
        throw std::runtime_error("missing scenario path");
    }
    if (options.command == "validate" && (!options.outputPath.empty() || options.compact)) {
        throw std::runtime_error("validate accepts neither --output nor --compact");
    }
    return options;
}

Scenario loadScenario(const std::string& path) {
    const std::string text = readFile(path);
    const Value root = mwi::json::parse(text);
    return Scenario::fromJson(root);
}

int dispatch(const Options& options) {
    if (options.command == "help") {
        printHelp();
        return 0;
    }
    if (options.command == "version") {
        const Value result = mwi::json::object({
            {"engine", Value::makeString("baritone-sim")},
            {"engine_version", Value::makeString(mwi::sim::kEngineVersion)},
            {"result_schema_version", Value::makeNumber(mwi::sim::kResultSchemaVersion)},
            {"cxx_standard", Value::makeNumber(__cplusplus)},
            {"license_scope", Value::makeString("first-party MIT; reference/ is unlicensed reading material")},
        });
        std::cout << mwi::json::stringify(result, !options.compact) << '\n';
        return 0;
    }

    Scenario scenario = loadScenario(options.scenarioPath);
    if (options.command == "validate") {
        const Value result = mwi::json::object({
            {"valid", Value::makeBool(true)},
            {"scenario", scenario.summary()},
        });
        std::cout << mwi::json::stringify(result, !options.compact) << '\n';
        return 0;
    }

    mwi::sim::RunResult run;
    try {
        run = mwi::sim::runScenario(scenario);
    } catch (const std::exception& error) {
        return printError("runtime_error", error.what(), 0, 3);
    } catch (...) {
        return printError("runtime_error", "unknown runtime exception", 0, 3);
    }
    const std::string text = mwi::json::stringify(run.toJson(), !options.compact) + "\n";
    if (options.outputPath.empty() || options.outputPath == "-") {
        std::cout << text;
    } else {
        writeFile(options.outputPath, text);
        const Value receipt = mwi::json::object({
            {"written", Value::makeString(options.outputPath)},
            {"status", Value::makeString(run.status)},
            {"ticks", Value::makeNumber(run.ticks)},
            {"breaks", Value::makeNumber(static_cast<double>(run.breaks.size()))},
        });
        std::cout << mwi::json::stringify(receipt, !options.compact) << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return dispatch(parseOptions(argc, argv));
    } catch (const mwi::json::ParseError& error) {
        return printError("json_parse_error", error.what(), error.position());
    } catch (const std::runtime_error& error) {
        return printError("validation_or_file_error", error.what());
    } catch (const std::exception& error) {
        return printError("runtime_error", error.what(), 0, 3);
    } catch (...) {
        return printError("unknown_error", "unknown exception", 0, 3);
    }
}
