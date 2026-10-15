#include "AnnieXMedia/utils/formatters.hpp"
#include "AnnieXMedia/core/git.hpp"
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace AnnieXMedia::utils::formatters {

std::string seconds_to_min(int seconds) {
    if (seconds <= 0) return "0:00";
    int mins = seconds / 60;
    int secs = seconds % 60;
    int hours = mins / 60;
    mins %= 60;
    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << ":" << std::setw(2) << std::setfill('0') << mins << ":"
            << std::setw(2) << std::setfill('0') << secs;
    } else {
        oss << mins << ":" << std::setw(2) << std::setfill('0') << secs;
    }
    return oss.str();
}

std::string get_readable_time(int64_t seconds) {
    int64_t count = 0;
    std::string ping_time;
    std::pair<std::string, int64_t> intervals[] = {{"d", 86400}, {"h", 3600}, {"m", 60}, {"s", 1}};
    for (const auto& [suffix, div] : intervals) {
        if (seconds >= div) {
            int64_t val = seconds / div;
            seconds %= div;
            if (!ping_time.empty()) ping_time += ":";
            ping_time += std::to_string(val) + suffix;
            if (++count >= 4) break;
        }
    }
    return ping_time.empty() ? "0s" : ping_time;
}

std::string convert_bytes(double size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (size >= 1024.0 && i < 4) {
        size /= 1024.0;
        ++i;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[i];
    return oss.str();
}

int check_duration(const std::string& file_path) {
    auto [out, rc] = core::run_shell_command(
        "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + file_path + "\""
    );
    if (rc == 0 && !out.empty()) {
        try { return static_cast<int>(std::stod(out)); } catch (...) {}
    }
    return 0;
}

} // namespace AnnieXMedia::utils::formatters
