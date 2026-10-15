#pragma once
#include <cstdint>
#include <string>
#include <tuple>

namespace AnnieXMedia::mongo {
bool usr_data(int64_t user_id);
std::tuple<std::string, std::string, std::string> get_userdata(int64_t user_id);
void add_userdata(int64_t user_id, const std::string& username, const std::string& first_name, const std::string& last_name);
bool check_pretender(int64_t chat_id);
void impo_on(int64_t chat_id);
void impo_off(int64_t chat_id);
} // namespace AnnieXMedia::mongo
