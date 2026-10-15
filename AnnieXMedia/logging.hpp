#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace AnnieXMedia {

class LoggerInstance {
    std::string name_;
    static std::mutex& get_mutex() {
        static std::mutex m;
        return m;
    }

    void write_log(const char* level, const char* color, const std::string& msg) const {
        std::lock_guard<std::mutex> lock(get_mutex());
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
        localtime_r(&t, &tm_buf);

        std::ostringstream oss;
        oss << "[" << std::put_time(&tm_buf, "%d-%b-%y %H:%M:%S") << " - " << level << "] - " << name_ << " - " << msg;
        std::string formatted = oss.str();

        std::cout << color << formatted << "\033[0m" << std::endl;

        std::ofstream logfile("log.txt", std::ios::app);
        if (logfile.is_open()) {
            logfile << formatted << "\n";
        }
    }

public:
    explicit LoggerInstance(std::string name) : name_(std::move(name)) {}

    void info(const std::string& msg) const { write_log("INFO", "\033[32m", msg); }
    void warning(const std::string& msg) const { write_log("WARNING", "\033[33m", msg); }
    void error(const std::string& msg) const { write_log("ERROR", "\033[31m", msg); }
    void debug(const std::string& msg) const { write_log("DEBUG", "\033[36m", msg); }
};

inline LoggerInstance LOGGER(const std::string& name) {
    return LoggerInstance(name);
}

} // namespace AnnieXMedia
