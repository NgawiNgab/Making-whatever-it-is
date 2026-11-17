#include <algorithm>
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
    std::vector<std::string> scenarioPaths;
    std::string outputPath;
    std::string batchOutDir;
    bool compact = false;
    bool expect = false;       // --expect <status>: fail the process on a different status
    std::string expectedStatus;
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

// "completed" is the only status that means the bot did what was asked. "no_target" is
// not a failure of the engine (the scenario may be a deliberate negative case), so it
// does not affect the exit code unless --expect asks for it. What must never happen is
// a CI job that treats "timeout" or "blocked" as success.
bool isSuccessfulStatus(std::string_view status) {
    return status == "completed" || status == "no_target";
}

void printHelp() {
    std::cout << R"(baritone-sim - headless autonomous-mining engine

Usage:
  baritone-sim version
  baritone-sim validate <scenario.json> [<scenario.json> ...]
  baritone-sim run <scenario.json> [<scenario.json> ...] [--output result.json]
                   [--batch-out DIR] [--expect completed] [--compact]

Commands:
  version   Print engine/schema/license metadata.
  validate  Parse and validate scenarios without running the bot.
  run       Execute a bounded simulation and print/write a JSON result.

Options:
  --output <path>    Write the result JSON to a file instead of stdout (one scenario only).
  --batch-out <dir>  Write <dir>/<scenario-name>.json per scenario; requires a name field
                     that is unique across the inputs. Prints a summary instead of results.
  --expect <status>  Exit 1 unless every run ends with that status (for automation).
  --compact          Emit single-line JSON (default is pretty-printed).

Multiple scenarios are run in order, in one process. Use --batch-out or --compact for
batch use; otherwise each result is printed in sequence.

Exit codes: 0 success, 1 a run did not complete as expected (timeout/blocked/
tool_insufficient), 2 usage/parse/validation/file error, 3 runtime error.
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
        } else if (arg == "--batch-out") {
            if (i + 1 >= argc) throw std::runtime_error("--batch-out requires a directory");
            options.batchOutDir = argv[++i];
        } else if (arg == "--expect") {
            if (i + 1 >= argc) throw std::runtime_error("--expect requires a status");
            options.expect = true;
            options.expectedStatus = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error("unknown option: " + arg);
        } else {
            options.scenarioPaths.push_back(arg);
        }
    }
    if (options.command == "version" && !options.scenarioPaths.empty()) {
        throw std::runtime_error("version does not take a scenario path");
    }
    if (options.command != "version" && options.scenarioPaths.empty()) {
        throw std::runtime_error("missing scenario path");
    }
    if (options.command == "validate" &&
        (!options.outputPath.empty() || !options.batchOutDir.empty() || options.expect)) {
        throw std::runtime_error("validate accepts none of --output/--batch-out/--expect");
    }
    if (!options.outputPath.empty() && options.scenarioPaths.size() > 1) {
        throw std::runtime_error("--output takes one scenario; use --batch-out for several");
    }
    if (!options.batchOutDir.empty() && !options.outputPath.empty()) {
        throw std::runtime_error("--output and --batch-out are mutually exclusive");
    }
    if (options.expect && options.expectedStatus != "completed" && options.expectedStatus != "no_target" &&
        options.expectedStatus != "timeout" && options.expectedStatus != "blocked" &&
        options.expectedStatus != "tool_insufficient") {
        throw std::runtime_error("--expect takes one of: completed, no_target, timeout, blocked, "
                                 "tool_insufficient");
    }
    return options;
}

Scenario loadScenario(const std::string& path) {
    const std::string text = readFile(path);
    const Value root = mwi::json::parse(text);
    return Scenario::fromJson(root);
}

// One safe file name per scenario. The name comes from user JSON, so it is reduced to
// [A-Za-z0-9._-]: a scenario called "../../etc/passwd" must not choose the output path.
std::string batchFileName(const Scenario& scenario, const std::string& fallbackPath,
                          std::size_t index) {
    std::string base = scenario.name;
    std::string safe;
    for (const char ch : base) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        safe.push_back(ok ? ch : '-');
    }
    while (!safe.empty() && (safe.front() == '.' || safe.front() == '-')) safe.erase(safe.begin());
    if (safe.empty()) {
        safe = std::filesystem::path(fallbackPath).stem().string();
        if (safe.empty()) safe = "scenario-" + std::to_string(index);
    }
    return safe + ".json";
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

    if (options.command == "validate") {
        std::vector<Value> entries;
        for (const std::string& path : options.scenarioPaths) {
            entries.push_back(mwi::json::object({
                {"path", Value::makeString(path)},
                {"valid", Value::makeBool(true)},
                {"scenario", loadScenario(path).summary()},
            }));
        }
        const Value result = options.scenarioPaths.size() == 1
                                 ? entries.front()
                                 : mwi::json::object({{"valid", Value::makeBool(true)},
                                                      {"files", Value::makeArray(std::move(entries))}});
        std::cout << mwi::json::stringify(result, !options.compact) << '\n';
        return 0;
    }

    std::vector<std::string> summaryLines;
    bool allExpected = true;
    bool wroteBatch = false;
    std::vector<std::string> usedNames;

    for (std::size_t index = 0; index < options.scenarioPaths.size(); ++index) {
        const std::string& path = options.scenarioPaths[index];
        Scenario scenario = loadScenario(path);

        mwi::sim::RunResult run;
        try {
            run = mwi::sim::runScenario(scenario);
        } catch (const std::exception& error) {
            return printError("runtime_error", error.what(), 0, 3);
        } catch (...) {
            return printError("runtime_error", "unknown runtime exception", 0, 3);
        }
        const bool expected =
            options.expect ? run.status == options.expectedStatus : isSuccessfulStatus(run.status);
        if (!expected) allExpected = false;

        const std::string text = mwi::json::stringify(run.toJson(), !options.compact) + "\n";
        if (!options.batchOutDir.empty()) {
            std::string fileName = batchFileName(scenario, path, index);
            // Two scenarios with the same name would silently overwrite each other.
            if (std::find(usedNames.begin(), usedNames.end(), fileName) != usedNames.end()) {
                fileName = std::filesystem::path(fileName).stem().string() + "-" +
                           std::to_string(index) + ".json";
            }
            usedNames.push_back(fileName);
            const std::string target = (std::filesystem::path(options.batchOutDir) / fileName).string();
            writeFile(target, text);
            wroteBatch = true;
            summaryLines.push_back(run.status);
            const Value receipt = mwi::json::object({
                {"written", Value::makeString(target)},
                {"scenario", Value::makeString(scenario.name)},
                {"status", Value::makeString(run.status)},
                {"ticks", Value::makeNumber(run.ticks)},
                {"breaks", Value::makeNumber(static_cast<double>(run.breaks.size()))},
                {"expected", Value::makeBool(expected)},
            });
            std::cout << mwi::json::stringify(receipt, !options.compact) << '\n';
        } else if (options.outputPath.empty() || options.outputPath == "-") {
            std::cout << text;
        } else {
            writeFile(options.outputPath, text);
            const Value receipt = mwi::json::object({
                {"written", Value::makeString(options.outputPath)},
                {"status", Value::makeString(run.status)},
                {"ticks", Value::makeNumber(run.ticks)},
                {"breaks", Value::makeNumber(static_cast<double>(run.breaks.size()))},
                {"expected", Value::makeBool(expected)},
            });
            std::cout << mwi::json::stringify(receipt, !options.compact) << '\n';
        }
    }

    if (wroteBatch) {
        std::cerr << "baritone-sim: " << options.scenarioPaths.size() << " scenario(s) written to "
                  << options.batchOutDir << (allExpected ? ", all expected" : ", some unexpected")
                  << '\n';
    }
    return allExpected ? 0 : 1;
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
