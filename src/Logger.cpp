#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <ctime>
#include <iostream>

namespace task_engine {

void StdLogOutput::write(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    // Thread-safe time conversion
    std::tm tm_snapshot{};
#if defined(_MSC_VER)
    localtime_s(&tm_snapshot, &time);
#else
    localtime_r(&time, &tm_snapshot);
#endif

    std::ostream* out = &std::cout;
    std::string levelStr = "[INFO] ";
    
    switch (level) {
        case LogLevel::DEBUG: levelStr = "[DEBUG]"; break;
        case LogLevel::INFO:  levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR: levelStr = "[ERROR]"; out = &std::cerr; break;
    }

    // Format: YYYY-MM-DD HH:MM:SS [LEVEL] Message
    *out << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S") << " " 
         << levelStr << " " << message << std::endl;
}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : output_(std::make_shared<StdLogOutput>()) {}

void Logger::set_output(std::shared_ptr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(mutex_);
    output_ = output;
}

void Logger::log(LogLevel level, const std::string& message) {
    // Mutex ensures thread safety for the output sink
    std::lock_guard<std::mutex> lock(mutex_);
    if (output_) {
        output_->write(level, message);
    }
}

Logger::LogStream::~LogStream() {
    // Flush the accumulated string to the logger
    logger_.log(level_, buffer_.str());
}

} // namespace task_engine