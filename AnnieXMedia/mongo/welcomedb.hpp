#pragma once
#include <cstdint>
#include <string>

namespace AnnieXMedia::mongo {
bool is_welcome_on(int64_t chat_id);
void set_welcome_state(int64_t chat_id, const std::string& flag);
} // namespace AnnieXMedia::mongo
