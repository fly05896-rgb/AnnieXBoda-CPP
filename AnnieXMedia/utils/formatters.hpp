#pragma once
#include <cstdint>
#include <string>

namespace AnnieXMedia::utils::formatters {
std::string seconds_to_min(int seconds);
std::string get_readable_time(int64_t seconds);
std::string convert_bytes(double size);
int check_duration(const std::string& file_path);
} // namespace AnnieXMedia::utils::formatters
