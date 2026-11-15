#include "platform/Log.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

#include <pl/Logger.hpp>

namespace logger {
namespace {

std::mutex& fileMutex() {
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path& logFile() {
    static std::filesystem::path path;
    return path;
}

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "D";
        case Level::Info: return "I";
        case Level::Warn: return "W";
        case Level::Error: return "E";
    }
    return "?";
}

std::ofstream& stream() {
    static std::ofstream out;
    return out;
}

void appendToFile(Level level, std::string_view message) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastFlush = Clock::now();

    std::lock_guard lock(fileMutex());
    const auto& path = logFile();
    if (path.empty()) return;

    auto& out = stream();
    if (!out.is_open()) {
        out.open(path, std::ios::app);
        if (!out) return;
    }

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    out << now << ' ' << levelName(level) << ' ' << message << '\n';

    // Opening/closing the file for every line stalled the game thread on slow storage.
    // Keep it open and flush on a timer (immediately for warnings and errors).
    const auto steady = Clock::now();
    if (level == Level::Warn || level == Level::Error ||
        steady - lastFlush >= std::chrono::milliseconds(500)) {
        out.flush();
        lastFlush = steady;
    }
}

pl::log::Logger& log() {
    static auto& instance = pl::log::Logger::getOrCreate("Baritone");
    return instance;
}

}  // namespace

void flush() {
    std::lock_guard lock(fileMutex());
    if (stream().is_open()) stream().flush();
}

void setLogFile(std::filesystem::path path) {
    std::lock_guard lock(fileMutex());
    if (stream().is_open()) stream().close();
    logFile() = std::move(path);
    if (!logFile().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(logFile().parent_path(), ec);
    }
}

void write(Level level, std::string_view message) {
    appendToFile(level, message);
    switch (level) {
        case Level::Debug: log().debug("{}", message); break;
        case Level::Info: log().info("{}", message); break;
        case Level::Warn: log().warn("{}", message); break;
        case Level::Error: log().error("{}", message); break;
    }
}

}  // namespace logger
