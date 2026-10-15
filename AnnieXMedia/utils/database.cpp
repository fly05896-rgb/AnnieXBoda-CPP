#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/core/userbot.hpp"
#include <algorithm>
#include <mutex>
#include <random>
#include <set>

namespace AnnieXMedia::utils::database {

static std::mutex cache_mtx;
static std::set<int64_t> active_set;
static std::set<int64_t> active_video_set;
static std::unordered_map<int64_t, int> assistant_dict;
static std::unordered_map<int64_t, std::string> lang_map;
static std::unordered_map<int64_t, int> loop_map;
static std::unordered_map<int64_t, bool> pause_map;
static std::unordered_map<int64_t, bool> mute_map;
static std::unordered_map<int64_t, int64_t> cmode_map;
static std::unordered_map<int64_t, std::string> playmode_map;
static std::unordered_map<int64_t, std::string> playtype_map;
static std::unordered_map<int64_t, bool> nonadmin_map;
static std::unordered_map<std::string, bool> song_cache;
static std::unordered_map<std::string, std::string> warehouse_cache;
static bool maintenance_flag = false;
static bool autoend_flag = false;

bool get_config(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (song_cache.count(key)) return song_cache[key];
    auto doc = core::mongodb.collection("song_settings")->find_one("song_config");
    bool val = doc ? doc->get_bool(key, false) : false;
    song_cache[key] = val;
    return val;
}

void set_config(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    song_cache[key] = value;
    core::mongodb.collection("song_settings")->set_field("song_config", key, value ? "1" : "0");
}

std::string get_cached_file(const std::string& query) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (warehouse_cache.count(query)) return warehouse_cache[query];
    auto doc = core::mongodb.collection("warehouse")->find_one(query);
    if (doc) {
        std::string fid = doc->get_str("file_id");
        warehouse_cache[query] = fid;
        return fid;
    }
    return "";
}

void cache_file(const std::string& query, const std::string& file_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    warehouse_cache[query] = file_id;
    core::mongodb.collection("warehouse")->set_field(query, "file_id", file_id);
}

int set_calls_assistant(int64_t chat_id) {
    int chosen = 1;
    if (!core::assistants.empty()) {
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, core::assistants.size() - 1);
        chosen = core::assistants[dist(rng)];
    }
    assistant_dict[chat_id] = chosen;
    core::mongodb.collection("assistants")->set_field(std::to_string(chat_id), "assistant", std::to_string(chosen));
    return chosen;
}

int get_assistant_index(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (assistant_dict.count(chat_id)) {
        int idx = assistant_dict[chat_id];
        if (std::find(core::assistants.begin(), core::assistants.end(), idx) != core::assistants.end()) {
            return idx;
        }
    }
    auto doc = core::mongodb.collection("assistants")->find_one(std::to_string(chat_id));
    if (doc) {
        int idx = static_cast<int>(doc->get_int("assistant", 1));
        if (std::find(core::assistants.begin(), core::assistants.end(), idx) != core::assistants.end()) {
            assistant_dict[chat_id] = idx;
            return idx;
        }
    }
    return set_calls_assistant(chat_id);
}

std::string get_lang(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (lang_map.count(chat_id)) return lang_map[chat_id];
    auto doc = core::mongodb.collection("language")->find_one(std::to_string(chat_id));
    std::string lang = doc ? doc->get_str("lang", "ar") : "ar";
    lang_map[chat_id] = lang;
    return lang;
}

void set_lang(int64_t chat_id, const std::string& lang) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    lang_map[chat_id] = lang;
    core::mongodb.collection("language")->set_field(std::to_string(chat_id), "lang", lang);
}

int get_loop(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    auto it = loop_map.find(chat_id);
    return it != loop_map.end() ? it->second : 0;
}

void set_loop(int64_t chat_id, int count) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    loop_map[chat_id] = count;
}

bool is_active_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return active_set.count(chat_id) > 0;
}

void add_active_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    active_set.insert(chat_id);
}

void remove_active_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    active_set.erase(chat_id);
}

std::vector<int64_t> get_active_chats() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return std::vector<int64_t>(active_set.begin(), active_set.end());
}

bool is_active_video_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return active_video_set.count(chat_id) > 0;
}

void add_active_video_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    active_video_set.insert(chat_id);
}

void remove_active_video_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    active_video_set.erase(chat_id);
}

std::vector<int64_t> get_active_video_chats() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return std::vector<int64_t>(active_video_set.begin(), active_video_set.end());
}

bool is_music_playing(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    auto it = pause_map.find(chat_id);
    return it != pause_map.end() ? it->second : false;
}

void music_on(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    pause_map[chat_id] = true;
}

void music_off(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    pause_map[chat_id] = false;
}

bool is_muted(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    auto it = mute_map.find(chat_id);
    return it != mute_map.end() ? it->second : false;
}

void mute_on(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    mute_map[chat_id] = true;
}

void mute_off(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    mute_map[chat_id] = false;
}

bool is_autoend() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return autoend_flag;
}

void autoend_on() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    autoend_flag = true;
    core::mongodb.collection("autoend")->set_field("autoend", "enabled", "1");
}

void autoend_off() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    autoend_flag = false;
    core::mongodb.collection("autoend")->delete_one("autoend");
}

bool is_maintenance() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    return maintenance_flag;
}

void maintenance_on() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    maintenance_flag = true;
    core::mongodb.collection("onoffper")->set_field("maintenance", "enabled", "1");
}

void maintenance_off() {
    std::lock_guard<std::mutex> lock(cache_mtx);
    maintenance_flag = false;
    core::mongodb.collection("onoffper")->delete_one("maintenance");
}

bool is_on_off(int flag) {
    auto doc = core::mongodb.collection("onoffper")->find_one(std::to_string(flag));
    return doc.has_value();
}

void add_on(int flag) {
    core::mongodb.collection("onoffper")->set_field(std::to_string(flag), "on", "1");
}

void add_off(int flag) {
    core::mongodb.collection("onoffper")->delete_one(std::to_string(flag));
}

int64_t get_cmode(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (cmode_map.count(chat_id)) return cmode_map[chat_id];
    auto doc = core::mongodb.collection("cplaymode")->find_one(std::to_string(chat_id));
    if (doc) {
        int64_t m = doc->get_int("mode", 0);
        cmode_map[chat_id] = m;
        return m;
    }
    return 0;
}

void set_cmode(int64_t chat_id, int64_t channel_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    cmode_map[chat_id] = channel_id;
    core::mongodb.collection("cplaymode")->set_field(std::to_string(chat_id), "mode", std::to_string(channel_id));
}

std::string get_playmode(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (playmode_map.count(chat_id)) return playmode_map[chat_id];
    auto doc = core::mongodb.collection("playmode")->find_one(std::to_string(chat_id));
    std::string mode = doc ? doc->get_str("mode", "Direct") : "Direct";
    playmode_map[chat_id] = mode;
    return mode;
}

void set_playmode(int64_t chat_id, const std::string& mode) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    playmode_map[chat_id] = mode;
    core::mongodb.collection("playmode")->set_field(std::to_string(chat_id), "mode", mode);
}

std::string get_playtype(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (playtype_map.count(chat_id)) return playtype_map[chat_id];
    auto doc = core::mongodb.collection("playtypedb")->find_one(std::to_string(chat_id));
    std::string pt = doc ? doc->get_str("mode", "Everyone") : "Everyone";
    playtype_map[chat_id] = pt;
    return pt;
}

void set_playtype(int64_t chat_id, const std::string& ptype) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    playtype_map[chat_id] = ptype;
    core::mongodb.collection("playtypedb")->set_field(std::to_string(chat_id), "mode", ptype);
}

bool is_nonadmin_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    if (nonadmin_map.count(chat_id)) return nonadmin_map[chat_id];
    auto doc = core::mongodb.collection("adminauth")->find_one(std::to_string(chat_id));
    bool val = doc.has_value();
    nonadmin_map[chat_id] = val;
    return val;
}

void add_nonadmin_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    nonadmin_map[chat_id] = true;
    core::mongodb.collection("adminauth")->set_field(std::to_string(chat_id), "nonadmin", "1");
}

void remove_nonadmin_chat(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(cache_mtx);
    nonadmin_map[chat_id] = false;
    core::mongodb.collection("adminauth")->delete_one(std::to_string(chat_id));
}

std::vector<std::string> get_authuser_names(int64_t chat_id) {
    std::vector<std::string> res;
    auto doc = core::mongodb.collection("authuser")->find_one(std::to_string(chat_id));
    if (doc) {
        for (const auto& [k, v] : doc->fields) {
            res.push_back(k);
        }
    }
    return res;
}

void save_authuser(int64_t chat_id, const std::string& token, const std::unordered_map<std::string, std::string>& data) {
    std::string uid = data.count("auth_user_id") ? data.at("auth_user_id") : token;
    core::mongodb.collection("authuser")->set_field(std::to_string(chat_id), token, uid);
}

bool delete_authuser(int64_t chat_id, const std::string& token) {
    auto col = core::mongodb.collection("authuser");
    auto doc = col->find_one(std::to_string(chat_id));
    if (!doc || !doc->fields.count(token)) return false;
    doc->fields.erase(token);
    col->update_one(std::to_string(chat_id), *doc, true);
    return true;
}

std::vector<int64_t> get_gbanned() {
    std::vector<int64_t> res;
    for (const auto& [pk, doc] : core::mongodb.collection("gban")->find_all()) {
        try { res.push_back(std::stoll(pk)); } catch (...) {}
    }
    return res;
}

void add_gban_user(int64_t user_id) {
    core::mongodb.collection("gban")->set_field(std::to_string(user_id), "user_id", std::to_string(user_id));
}

void remove_gban_user(int64_t user_id) {
    core::mongodb.collection("gban")->delete_one(std::to_string(user_id));
}

std::vector<int64_t> get_banned_users() {
    std::vector<int64_t> res;
    for (const auto& [pk, doc] : core::mongodb.collection("blockedusers")->find_all()) {
        try { res.push_back(std::stoll(pk)); } catch (...) {}
    }
    return res;
}

void add_banned_user(int64_t user_id) {
    core::mongodb.collection("blockedusers")->set_field(std::to_string(user_id), "user_id", std::to_string(user_id));
}

void remove_banned_user(int64_t user_id) {
    core::mongodb.collection("blockedusers")->delete_one(std::to_string(user_id));
}

std::vector<int64_t> blacklisted_chats() {
    std::vector<int64_t> res;
    for (const auto& [pk, doc] : core::mongodb.collection("blacklistChat")->find_all()) {
        try { res.push_back(std::stoll(pk)); } catch (...) {}
    }
    return res;
}

bool blacklist_chat(int64_t chat_id) {
    core::mongodb.collection("blacklistChat")->set_field(std::to_string(chat_id), "chat_id", std::to_string(chat_id));
    return true;
}

bool whitelist_chat(int64_t chat_id) {
    return core::mongodb.collection("blacklistChat")->delete_one(std::to_string(chat_id));
}

std::vector<int64_t> get_served_chats() {
    std::vector<int64_t> res;
    for (const auto& [pk, doc] : core::mongodb.collection("chats")->find_all()) {
        try { res.push_back(std::stoll(pk)); } catch (...) {}
    }
    return res;
}

void add_served_chat(int64_t chat_id) {
    core::mongodb.collection("chats")->set_field(std::to_string(chat_id), "chat_id", std::to_string(chat_id));
}

std::vector<int64_t> get_served_users() {
    std::vector<int64_t> res;
    for (const auto& [pk, doc] : core::mongodb.collection("tgusersdb")->find_all()) {
        try { res.push_back(std::stoll(pk)); } catch (...) {}
    }
    return res;
}

void add_served_user(int64_t user_id) {
    core::mongodb.collection("tgusersdb")->set_field(std::to_string(user_id), "user_id", std::to_string(user_id));
}

std::unordered_map<std::string, std::string> get_playlist(int64_t user_id) {
    auto doc = core::mongodb.collection("playlist")->find_one(std::to_string(user_id));
    if (!doc) return {};
    return doc->fields;
}

void save_playlist(int64_t user_id, const std::string& vidid, const std::string& title) {
    core::mongodb.collection("playlist")->set_field(std::to_string(user_id), vidid, title);
}

void delete_playlist(int64_t user_id, const std::string& vidid) {
    if (vidid.empty()) {
        core::mongodb.collection("playlist")->delete_one(std::to_string(user_id));
        return;
    }
    auto col = core::mongodb.collection("playlist");
    auto doc = col->find_one(std::to_string(user_id));
    if (doc && doc->fields.count(vidid)) {
        doc->fields.erase(vidid);
        col->update_one(std::to_string(user_id), *doc, true);
    }
}

} // namespace AnnieXMedia::utils::database
