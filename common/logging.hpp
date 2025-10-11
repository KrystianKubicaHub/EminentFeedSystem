#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace std;

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class LoggerConfig {
public:
    static void setLevel(LogLevel level);
    static LogLevel level();
    static void setThrottleDuration(chrono::milliseconds duration);
    static chrono::milliseconds throttleDuration();
private:
    static atomic<int> levelValue_;
    static atomic<long long> throttleDurationMs_;
};

class LoggerBase {
protected:
    explicit LoggerBase(string className = "Default");
    void setLoggerClassName(const string& name);
    const string& loggerClassName() const;
    void log(LogLevel level, const string& message);
private:
    struct ThrottleState {
        long long lastLogTimeMs = 0;
        size_t suppressedCount = 0;
    };

    string className_;
    static mutex outputMutex_;
    static mutex throttleMutex_;
    static unordered_map<string, ThrottleState> throttleState_;

    void emitLog(LogLevel level, const string& message);
    static string levelToString(LogLevel level);
    static long long currentTimeMs();
    static string timestampNow();
};
