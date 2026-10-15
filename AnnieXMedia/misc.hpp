#pragma once

#include "config.hpp"
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AnnieXMedia {

extern std::unordered_set<int64_t> SUDOERS;
extern std::unordered_map<int64_t, std::vector<config::QueueItem>> db;
extern std::mutex db_mutex;
extern std::time_t _boot_;

bool is_heroku();
bool is_sudo(int64_t user_id);
void dbb();
void sudo();
void heroku();

} // namespace AnnieXMedia
