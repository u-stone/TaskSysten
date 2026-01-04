#pragma once

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <TaskEngine/TaskEngineExport.h>

namespace task_engine {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

/**
 * @brief Interface for custom log output implementations.
 *        Users can implement this to redirect logs to files, network, etc.
 */
class TASK_ENGINE_EXPORT ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void write(LogLevel level, const std::string& message) = 0;
};

/**
 * @brief Default log output implementation using std::cout and std::cerr.
 */
class TASK_ENGINE_EXPORT StdLogOutput : public ILogOutput {
public:
    void write(LogLevel level, const std::string& message) override;
};

/**
 * @brief Thread-safe Singleton Logger class.
 */
class TASK_ENGINE_EXPORT Logger {
public:
    static Logger& instance();

    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Sets a custom output implementation.
     * @param output Shared pointer to the custom output handler.
     */
    void set_output(std::shared_ptr<ILogOutput> output);

    /**
     * @brief Logs a message with a specific level.
     * @param level The severity level.
     * @param message The message string.
     */
    void log(LogLevel level, const std::string& message);

    /**
     * @brief Helper class for stream-style logging (e.g., LOG_INFO() << "msg").
     */
    class LogStream {
    public:
        LogStream(LogLevel level, Logger& logger) : level_(level), logger_(logger) {}
        
        // Destructor flushes the buffer to the logger
        ~LogStream();

        template <typename T>
        LogStream& operator<<(const T& val) {
            buffer_ << val;
            return *this;
        }

    private:
        LogLevel level_;
        Logger& logger_;
        std::ostringstream buffer_;
    };

    LogStream GetStream(LogLevel level) {
        return LogStream(level, *this);
    }

private:
    Logger();
    std::shared_ptr<ILogOutput> output_;
    std::mutex mutex_;
};

// Convenience macros for easy logging
#define LOG_DEBUG() task_engine::Logger::instance().GetStream(task_engine::LogLevel::DEBUG)
#define LOG_INFO()  task_engine::Logger::instance().GetStream(task_engine::LogLevel::INFO)
#define LOG_WARN()  task_engine::Logger::instance().GetStream(task_engine::LogLevel::WARNING)
#define LOG_ERROR() task_engine::Logger::instance().GetStream(task_engine::LogLevel::ERROR)

} // namespace task_engine