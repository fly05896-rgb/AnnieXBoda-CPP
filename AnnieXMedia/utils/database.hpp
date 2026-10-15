#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace AnnieXMedia::utils::database {

bool get_config(const std::string& key);
void set_config(const std::string& key, bool value);

std::string get_cached_file(const std::string& query);
void cache_file(const std::string& query, const std::string& file_id);

int get_assistant_index(int64_t chat_id);
int set_calls_assistant(int64_t chat_id);

std::string get_lang(int64_t chat_id);
void set_lang(int64_t chat_id, const std::string& lang);

int get_loop(int64_t chat_id);
void set_loop(int64_t chat_id, int count);

bool is_active_chat(int64_t chat_id);
void add_active_chat(int64_t chat_id);
void remove_active_chat(int64_t chat_id);
std::vector<int64_t> get_active_chats();

bool is_active_video_chat(int64_t chat_id);
void add_active_video_chat(int64_t chat_id);
void remove_active_video_chat(int64_t chat_id);
std::vector<int64_t> get_active_video_chats();

bool is_music_playing(int64_t chat_id);
void music_on(int64_t chat_id);
void music_off(int64_t chat_id);

bool is_muted(int64_t chat_id);
void mute_on(int64_t chat_id);
void mute_off(int64_t chat_id);

bool is_autoend();
void autoend_on();
void autoend_off();

bool is_maintenance();
void maintenance_on();
void maintenance_off();

bool is_on_off(int flag);
void add_on(int flag);
void add_off(int flag);

int64_t get_cmode(int64_t chat_id);
void set_cmode(int64_t chat_id, int64_t channel_id);

std::string get_playmode(int64_t chat_id);
void set_playmode(int64_t chat_id, const std::string& mode);

std::string get_playtype(int64_t chat_id);
void set_playtype(int64_t chat_id, const std::string& ptype);

bool is_nonadmin_chat(int64_t chat_id);
void add_nonadmin_chat(int64_t chat_id);
void remove_nonadmin_chat(int64_t chat_id);

std::vector<std::string> get_authuser_names(int64_t chat_id);
void save_authuser(int64_t chat_id, const std::string& token, const std::unordered_map<std::string, std::string>& data);
bool delete_authuser(int64_t chat_id, const std::string& token);

std::vector<int64_t> get_gbanned();
void add_gban_user(int64_t user_id);
void remove_gban_user(int64_t user_id);

std::vector<int64_t> get_banned_users();
void add_banned_user(int64_t user_id);
void remove_banned_user(int64_t user_id);

std::vector<int64_t> blacklisted_chats();
bool blacklist_chat(int64_t chat_id);
bool whitelist_chat(int64_t chat_id);

std::vector<int64_t> get_served_chats();
void add_served_chat(int64_t chat_id);

std::vector<int64_t> get_served_users();
void add_served_user(int64_t user_id);

std::unordered_map<std::string, std::string> get_playlist(int64_t user_id);
void save_playlist(int64_t user_id, const std::string& vidid, const std::string& title);
void delete_playlist(int64_t user_id, const std::string& vidid = "");

} // namespace AnnieXMedia::utils::database
