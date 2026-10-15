#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace AnnieXMedia::mongo {
struct CoupleData {
    int64_t user1 = 0;
    int64_t user2 = 0;
    std::string img;
};
std::optional<CoupleData> get_couple(int64_t cid, const std::string& date);
void save_couple(int64_t cid, const std::string& date, int64_t u1, int64_t u2, const std::string& img_path);
} // namespace AnnieXMedia::mongo
