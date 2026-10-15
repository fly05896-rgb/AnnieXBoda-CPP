// ── 𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ © 2026 (C++20 Edition - TDLib & Native NTgCalls) ──────────
// Converted from config.py with 100% identical environment variable names.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace config {

struct QueueItem {
    std::string title;
    std::string dur;
    std::string streamtype; // "audio", "video", "live", "index"
    std::string by;
    int64_t user_id = 0;
    int64_t chat_id = 0;
    std::string file;
    std::string vidid;
    int seconds = 0;
    int played = 0;
    int old_second = 0;
    std::string old_dur;
    std::string speed_path;
    double speed = 1.0;
    int64_t mystic_msg_id = 0;
    std::string markup;
};

// Load .env file into environment variables
void load_dotenv(const std::string& path = ".env");

// Helpers
int time_to_seconds(const std::string& time_str);
std::string getenv_str(const char* name, const std::string& def = "");
int64_t getenv_int(const char* name, int64_t def = 0);
bool getenv_bool(const char* name, bool def = false);

// ── Core bot config (إعدادات البوت الأساسية) ───────────────────────────────────
extern int32_t API_ID;
extern std::string API_HASH;
extern std::string BOT_TOKEN;

// معلومات المالك
extern int64_t OWNER_ID;
extern std::string OWNER_USERNAME;

// معلومات البوت والمساعد
extern std::string BOT_USERNAME;
extern std::string BOT_NAME;
extern std::string ASSUSERNAME;

// ── المسارات النووية (Nuclear Paths) ─────────────────────────────────────────
extern std::string DOWNLOAD_PATH;
extern std::string AUTO_DOWNLOADS_CLEAR;

// ── Database & logging ─────────────────────────────────────────────────────────
extern std::string MONGO_DB_URI;
extern int64_t LOGGER_ID;

// ── Limits & AI Groups ─────────────────────────────────────────────────────────
extern int DURATION_LIMIT_MIN;
extern int DURATION_LIMIT;
extern int SONG_DOWNLOAD_DURATION;
extern int SONG_DOWNLOAD_DURATION_LIMIT;
extern int64_t TG_AUDIO_FILESIZE_LIMIT;
extern int64_t TG_VIDEO_FILESIZE_LIMIT;
extern int PLAYLIST_FETCH_LIMIT;
extern int AI_HANDLER_GROUP;
extern int SERVER_PLAYLIST_LIMIT;

// ── External APIs ──────────────────────────────────────────────────────────────
extern std::string COOKIE_URL;
extern std::string API_URL;
extern std::string VIDEO_API_URL;
extern std::string API_KEY;
extern std::string DEEP_API;
extern std::string AI_API_KEY; // Loaded from GPT_4 env var

// ── Hosting / deployment ───────────────────────────────────────────────────────
extern std::string HEROKU_APP_NAME;
extern std::string HEROKU_API_KEY;

// ── Git / updates ──────────────────────────────────────────────────────────────
extern std::string UPSTREAM_REPO;
extern std::string UPSTREAM_BRANCH;
extern std::string GIT_TOKEN;

// ── Support links ──────────────────────────────────────────────────────────────
extern std::string SUPPORT_CHANNEL;
extern std::string SUPPORT_CHAT;

// ── Assistant auto-leave ───────────────────────────────────────────────────────
extern std::string AUTO_LEAVING_ASSISTANT;
extern int AUTO_LEAVE_ASSISTANT_TIME;
extern bool AUTO_END;

// ── Debug ──────────────────────────────────────────────────────────────────────
extern bool DEBUG_IGNORE_LOG;

// ── Spotify (optional) ─────────────────────────────────────────────────────────
extern std::string SPOTIFY_CLIENT_ID;
extern std::string SPOTIFY_CLIENT_SECRET;

// ── Session strings (نفس المتغيرات المحفوظة + دعم أرقام الهواتف في TDLib) ────
extern std::string STRING1;
extern std::string STRING2;
extern std::string STRING3;
extern std::string STRING4;
extern std::string STRING5;

// ── Media assets ───────────────────────────────────────────────────────────────
extern std::vector<std::string> START_VIDS;
extern std::vector<std::string> STICKERS;
extern std::string UNIFIED_IMG;
extern std::string START_IMG_URL;
extern std::string HELP_IMG_URL;
extern std::string PING_VID_URL;
extern std::string PLAYLIST_IMG_URL;
extern std::string STATS_VID_URL;
extern std::string TELEGRAM_AUDIO_URL;
extern std::string TELEGRAM_VIDEO_URL;
extern std::string STREAM_IMG_URL;
extern std::string SOUNCLOUD_IMG_URL;
extern std::string YOUTUBE_IMG_URL;
extern std::string SPOTIFY_ARTIST_IMG_URL;
extern std::string SPOTIFY_ALBUM_IMG_URL;
extern std::string SPOTIFY_PLAYLIST_IMG_URL;

// ── نصوص التشغيل والتحميل والستارت ───────────────────────────────────────────
extern std::vector<std::string> AYU;
extern std::vector<std::string> AYUV;

// ── Runtime structures ─────────────────────────────────────────────────────────
extern std::unordered_set<int64_t> BANNED_USERS;
extern std::unordered_map<int64_t, std::vector<int64_t>> adminlist;
extern std::unordered_map<std::string, std::string> lyrical;
extern std::vector<std::string> autoclean;
extern std::unordered_map<int64_t, std::unordered_map<std::string, std::string>> confirmer;
extern std::mutex runtime_mutex;

void init_config();

} // namespace config
