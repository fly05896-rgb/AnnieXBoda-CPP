#include "config.hpp"

namespace config {

void load_dotenv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        // Trim whitespace and quotes
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t' || val.front() == '"' || val.front() == '\'')) val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r' || val.back() == '"' || val.back() == '\'')) val.pop_back();
        if (!key.empty() && !val.empty()) {
            setenv(key.c_str(), val.c_str(), 0);
        }
    }
}

std::string getenv_str(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return (val && *val) ? std::string(val) : def;
}

int64_t getenv_int(const char* name, int64_t def) {
    const char* val = std::getenv(name);
    if (!val || !*val) return def;
    try {
        return std::stoll(val);
    } catch (...) {
        return def;
    }
}

bool getenv_bool(const char* name, bool def) {
    std::string val = getenv_str(name, def ? "True" : "False");
    return (val == "1" || val == "true" || val == "True" || val == "TRUE" || val == "yes" || val == "on");
}

int time_to_seconds(const std::string& time_str) {
    try {
        std::vector<int> parts;
        std::stringstream ss(time_str);
        std::string item;
        while (std::getline(ss, item, ':')) {
            parts.push_back(std::stoi(item));
        }
        int total = 0;
        int mult = 1;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            total += (*it) * mult;
            mult *= 60;
        }
        return total;
    } catch (...) {
        return 0;
    }
}

int32_t API_ID = 0;
std::string API_HASH;
std::string BOT_TOKEN;
int64_t OWNER_ID = 8313557781LL;
std::string OWNER_USERNAME = "𝐁ᴏᴅᴀ˼";
std::string BOT_USERNAME = "Boda";
std::string BOT_NAME = "˹𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ˼ ♪";
std::string ASSUSERNAME = "S_G0C7";
std::string DOWNLOAD_PATH = "/dev/shm/AnnieDownloads";
std::string AUTO_DOWNLOADS_CLEAR = "True";
std::string MONGO_DB_URI;
int64_t LOGGER_ID = -1004295349964LL;
int DURATION_LIMIT_MIN = 600;
int DURATION_LIMIT = 36000;
int SONG_DOWNLOAD_DURATION = 3600;
int SONG_DOWNLOAD_DURATION_LIMIT = 5400;
int64_t TG_AUDIO_FILESIZE_LIMIT = 2147483648LL;
int64_t TG_VIDEO_FILESIZE_LIMIT = 2147483648LL;
int PLAYLIST_FETCH_LIMIT = 100;
int AI_HANDLER_GROUP = 30;
int SERVER_PLAYLIST_LIMIT = 30;
std::string COOKIE_URL;
std::string API_URL = "https://hyperionengine.fly.dev";
std::string VIDEO_API_URL = "https://hyperionengine.fly.dev";
std::string API_KEY;
std::string DEEP_API;
std::string AI_API_KEY;
std::string HEROKU_APP_NAME;
std::string HEROKU_API_KEY;
std::string UPSTREAM_REPO = "https://t.me/SourceBoda";
std::string UPSTREAM_BRANCH = "Master";
std::string GIT_TOKEN;
std::string SUPPORT_CHANNEL = "https://t.me/SourceBoda";
std::string SUPPORT_CHAT = "https://t.me/music0587";
std::string AUTO_LEAVING_ASSISTANT = "False";
int AUTO_LEAVE_ASSISTANT_TIME = 3600;
bool AUTO_END = false;
bool DEBUG_IGNORE_LOG = true;
std::string SPOTIFY_CLIENT_ID = "22b6125bfe224587b722d6815002db2b";
std::string SPOTIFY_CLIENT_SECRET = "c9c63c6fbf2f467c8bc68624851e9773";
std::string STRING1;
std::string STRING2;
std::string STRING3;
std::string STRING4;
std::string STRING5;

std::vector<std::string> START_VIDS = {
    "https://files.catbox.moe/b6533n.jpg",
    "https://files.catbox.moe/wqipfn.jpg",
    "https://files.catbox.moe/efzuds.jpg",
};

std::vector<std::string> STICKERS = {
    "CAACAgQAAyEFAATHCHTJAAIToGlfMcgnOpNnuYnm1hlBTW_pZgZfAAIfFgAC-CS4UbtZNHZyyA3BHgQ",
    "CAACAgUAAyEFAATHCHTJAAITn2lfMb5VpY0QAom50knojYHju4bTAAILFQAC-vEZVMBmWHCQ-sJuHgQ",
};

std::string UNIFIED_IMG = "https://files.catbox.moe/ompn1o.jpg";
std::string START_IMG_URL = UNIFIED_IMG;
std::string HELP_IMG_URL = UNIFIED_IMG;
std::string PING_VID_URL = UNIFIED_IMG;
std::string PLAYLIST_IMG_URL = UNIFIED_IMG;
std::string STATS_VID_URL = UNIFIED_IMG;
std::string TELEGRAM_AUDIO_URL = UNIFIED_IMG;
std::string TELEGRAM_VIDEO_URL = UNIFIED_IMG;
std::string STREAM_IMG_URL = UNIFIED_IMG;
std::string SOUNCLOUD_IMG_URL = UNIFIED_IMG;
std::string YOUTUBE_IMG_URL = UNIFIED_IMG;
std::string SPOTIFY_ARTIST_IMG_URL = UNIFIED_IMG;
std::string SPOTIFY_ALBUM_IMG_URL = UNIFIED_IMG;
std::string SPOTIFY_PLAYLIST_IMG_URL = UNIFIED_IMG;

std::vector<std::string> AYU = {
    "جـاري الـتـشـغـيـل .. 🤍",
    "جـاري الـتـحـمـيـل .. 🫶",
    "لـحـظـة مـن فـضـلـك .. 🤍",
    "طـلـبـك قـيـد الـتـنـفـيـذ .. ☔",
    "يـتـم تـشـغـيـل الـتـراك .. 💝"
};

std::vector<std::string> AYUV = {
    "صـلـي عـلـي الـنـبـي وتـبـسـم 🤍🌿.\n"
    "مـرحـبـا انـا بـوت تـشـغـيـل صوتيات متطور ☔\n"
    "أهـلاً بـك عـزيـزي {0} 🫶\n"
    "وظـيـفـتـي تـشـغـيـل الـمـيـديـا فـي الـمـكـالـمـات بـجـودة عـالـيـة وبـدون تـقـطـيـع 🤍.\n"
    "⧉ لـتـشـغـيـل أغـنـيـة اكـتـب « تشغيل + اسم الاغنية »\n"
    "⧉ لـلـتـحـكـم فـي الـبـوت اضـغـط « الأوامـر » بـالأسـفـل 💝.\n"
    "ـــــــــــــــــــــــــــــــــــــــــــــــــــــــ\n"
    "⧉ وقـت الـعـمـل : {2}\n"
    "⧉ الـرام : {5}\n"
    "ـــــــــــــــــــــــــــــــــــــــــــــــــــــــ",
    "صـلـي عـلـي الـنـبـي وتـبـسـم 🤍🌿.\n"
    "مـرحـبـا انـا بـوت تـشـغـيـل صوتيات متطور ☔\n"
    "أهـلاً بـكـم فـي {0} 🫶\n"
    "أنـا {1} .. جـاهـز لـخـدمـتـكـم 🤍.\n"
    "⧉ أعـمـل بـكـفـاءة عـالـيـة بـدون تـوقـف.\n"
    "⧉ فـقـط أضـفـنـي لـمـجـمـوعـتـك وارفـعـنـي مـشـرف 💝.\n"
    "⧉ وقـت الـتـشـغـيـل : {2}"
};

std::unordered_set<int64_t> BANNED_USERS;
std::unordered_map<int64_t, std::vector<int64_t>> adminlist;
std::unordered_map<std::string, std::string> lyrical;
std::vector<std::string> autoclean;
std::unordered_map<int64_t, std::unordered_map<std::string, std::string>> confirmer;
std::mutex runtime_mutex;

void init_config() {
    load_dotenv(".env");

    API_ID = static_cast<int32_t>(getenv_int("API_ID", 0));
    API_HASH = getenv_str("API_HASH", "");
    if (API_ID == 0 || API_HASH.empty()) {
        std::cerr << "🚫 خطأ: يجب وضع API_ID و API_HASH في متغيرات النظام (.env) ليعمل البوت.\n";
        std::exit(1);
    }

    BOT_TOKEN = getenv_str("BOT_TOKEN", "");
    OWNER_ID = getenv_int("OWNER_ID", 8313557781LL);
    OWNER_USERNAME = getenv_str("OWNER_USERNAME", "𝐁ᴏᴅᴀ˼");
    BOT_USERNAME = getenv_str("BOT_USERNAME", "Boda");
    BOT_NAME = getenv_str("BOT_NAME", "˹𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ˼ ♪");
    ASSUSERNAME = getenv_str("ASSUSERNAME", "S_G0C7");

    if (std::filesystem::exists("/dev/shm")) {
        DOWNLOAD_PATH = "/dev/shm/AnnieDownloads";
    } else {
        DOWNLOAD_PATH = "downloads";
    }
    std::filesystem::create_directories(DOWNLOAD_PATH);

    AUTO_DOWNLOADS_CLEAR = getenv_str("AUTO_DOWNLOADS_CLEAR", "True");
    MONGO_DB_URI = getenv_str("MONGO_DB_URI", "");
    LOGGER_ID = getenv_int("LOGGER_ID", -1004295349964LL);

    DURATION_LIMIT_MIN = static_cast<int>(getenv_int("DURATION_LIMIT", 600));
    DURATION_LIMIT = time_to_seconds(std::to_string(DURATION_LIMIT_MIN) + ":00");
    SONG_DOWNLOAD_DURATION = static_cast<int>(getenv_int("SONG_DOWNLOAD_DURATION", 3600));
    SONG_DOWNLOAD_DURATION_LIMIT = static_cast<int>(getenv_int("SONG_DOWNLOAD_DURATION_LIMIT", 5400));
    TG_AUDIO_FILESIZE_LIMIT = getenv_int("TG_AUDIO_FILESIZE_LIMIT", 2147483648LL);
    TG_VIDEO_FILESIZE_LIMIT = getenv_int("TG_VIDEO_FILESIZE_LIMIT", 2147483648LL);
    PLAYLIST_FETCH_LIMIT = static_cast<int>(getenv_int("PLAYLIST_FETCH_LIMIT", 100));
    AI_HANDLER_GROUP = static_cast<int>(getenv_int("AI_HANDLER_GROUP", 30));
    SERVER_PLAYLIST_LIMIT = static_cast<int>(getenv_int("SERVER_PLAYLIST_LIMIT", 30));

    COOKIE_URL = getenv_str("COOKIE_URL", "");
    API_URL = getenv_str("API_URL", "https://hyperionengine.fly.dev");
    VIDEO_API_URL = getenv_str("VIDEO_API_URL", "https://hyperionengine.fly.dev");
    API_KEY = getenv_str("API_KEY", "");
    DEEP_API = getenv_str("DEEP_API", "");
    AI_API_KEY = getenv_str("GPT_4", "");

    HEROKU_APP_NAME = getenv_str("HEROKU_APP_NAME", "");
    HEROKU_API_KEY = getenv_str("HEROKU_API_KEY", "");
    UPSTREAM_REPO = getenv_str("UPSTREAM_REPO", "https://t.me/SourceBoda");
    UPSTREAM_BRANCH = getenv_str("UPSTREAM_BRANCH", "Master");
    GIT_TOKEN = getenv_str("GIT_TOKEN", "");
    SUPPORT_CHANNEL = getenv_str("SUPPORT_CHANNEL", "https://t.me/SourceBoda");
    SUPPORT_CHAT = getenv_str("SUPPORT_CHAT", "https://t.me/music0587");

    AUTO_LEAVING_ASSISTANT = getenv_str("AUTO_LEAVING_ASSISTANT", "False");
    AUTO_LEAVE_ASSISTANT_TIME = static_cast<int>(getenv_int("ASSISTANT_LEAVE_TIME", 3600));
    SPOTIFY_CLIENT_ID = getenv_str("SPOTIFY_CLIENT_ID", "22b6125bfe224587b722d6815002db2b");
    SPOTIFY_CLIENT_SECRET = getenv_str("SPOTIFY_CLIENT_SECRET", "c9c63c6fbf2f467c8bc68624851e9773");

    STRING1 = getenv_str("STRING_SESSION", "");
    STRING2 = getenv_str("STRING_SESSION2", "");
    STRING3 = getenv_str("STRING_SESSION3", "");
    STRING4 = getenv_str("STRING_SESSION4", "");
    STRING5 = getenv_str("STRING_SESSION5", "");

    std::regex url_re("^https?://");
    if (!SUPPORT_CHANNEL.empty() && !std::regex_search(SUPPORT_CHANNEL, url_re)) {
        std::cerr << "[ERROR] - Invalid SUPPORT_CHANNEL URL.\n";
        std::exit(1);
    }
    if (!SUPPORT_CHAT.empty() && !std::regex_search(SUPPORT_CHAT, url_re)) {
        std::cerr << "[ERROR] - Invalid SUPPORT_CHAT URL.\n";
        std::exit(1);
    }
}

} // namespace config
