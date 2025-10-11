#include "logging.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

using namespace std;

atomic<int> LoggerConfig::levelValue_{static_cast<int>(LogLevel::NONE)};
atomic<long long> LoggerConfig::throttleDurationMs_{0};

void LoggerConfig::setLevel(LogLevel level) {
    levelValue_.store(static_cast<int>(level), memory_order_relaxed);
}

LogLevel LoggerConfig::level() {
    return static_cast<LogLevel>(levelValue_.load(memory_order_relaxed));
}

void LoggerConfig::setThrottleDuration(chrono::milliseconds duration) {
    throttleDurationMs_.store(duration.count(), memory_order_relaxed);
}

chrono::milliseconds LoggerConfig::throttleDuration() {
    return chrono::milliseconds{throttleDurationMs_.load(memory_order_relaxed)};
}

mutex LoggerBase::outputMutex_;
mutex LoggerBase::throttleMutex_;
unordered_map<string, LoggerBase::ThrottleState> LoggerBase::throttleState_;

LoggerBase::LoggerBase(string className)
    : className_(move(className)) {}

void LoggerBase::setLoggerClassName(const string& name) {
    className_ = name;
}

const string& LoggerBase::loggerClassName() const {
    return className_;
}

void LoggerBase::log(LogLevel level, const string& message) {
    if (static_cast<int>(level) < static_cast<int>(LoggerConfig::level())) {
        return;
    }
    emitLog(level, message);
}

void LoggerBase::emitLog(LogLevel level, const string& message) {
    auto throttleWindow = LoggerConfig::throttleDuration();
    if (throttleWindow.count() > 0 && (level == LogLevel::WARN || level == LogLevel::ERROR)) {
        string key = className_ + "|" + levelToString(level) + "|" + message;
        ThrottleState state;
        {
            lock_guard<mutex> lock(throttleMutex_);
            auto it = throttleState_.find(key);
            long long now = currentTimeMs();
            if (it == throttleState_.end()) {
                state.lastLogTimeMs = now;
                throttleState_[key] = state;
            } else {
                state = it->second;
                if (now - state.lastLogTimeMs < throttleWindow.count()) {
                    it->second.suppressedCount += 1;
                    return;
                }
                it->second.lastLogTimeMs = now;
                state = it->second;
            }
        }

        size_t suppressed = 0;
        {
            lock_guard<mutex> lock(throttleMutex_);
            auto it = throttleState_.find(key);
            if (it != throttleState_.end()) {
                suppressed = it->second.suppressedCount;
                it->second.suppressedCount = 0;
            }
        }

        ostringstream oss;
        oss << message;
        if (suppressed > 0) {
            oss << " (suppressed " << suppressed << " repeats)";
        }

        lock_guard<mutex> lock(outputMutex_);
        cout << "[" << timestampNow() << "][" << className_ << "][" << levelToString(level) << "] "
             << oss.str() << endl;
        return;
    }

    lock_guard<mutex> lock(outputMutex_);
    cout << "[" << timestampNow() << "][" << className_ << "][" << levelToString(level) << "] "
         << message << endl;
}

string LoggerBase::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::NONE:
            return "NONE";
        default:
            return "INFO";
    }
}

long long LoggerBase::currentTimeMs() {
    auto now = chrono::steady_clock::now().time_since_epoch();
    return chrono::duration_cast<chrono::milliseconds>(now).count();
}

string LoggerBase::timestampNow() {
    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);
    tm tmBuffer{};
#ifdef _WIN32
    localtime_s(&tmBuffer, &time);
#else
    localtime_r(&time, &tmBuffer);
#endif
    ostringstream oss;
    oss << put_time(&tmBuffer, "%Y-%m-%dT%H:%M:%S");
    auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    oss << '.' << setw(3) << setfill('0') << ms;
    return oss.str();
}
