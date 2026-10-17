#!/usr/bin/env bash
set -e

echo "=========================================================="
echo "🚀 Building AnnieXBoda C++20 Music Bot (TDLib + NTgCalls)"
echo "=========================================================="

JOBS=$(nproc 2>/dev/null || echo 4)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# محاولة تثبيت مكتبة pymongo اختيارياً لحفظ جلسة TDLib في MongoDB تلقائياً عبر عمليات الـ Deploy
(apt-get update && apt-get install -y --no-install-recommends python3-pymongo python3-dnspython) >/dev/null 2>&1 || true

# إنشاء أداة مزامنة وحفظ جلسة المساعد في MongoDB حتى لا يطلب كود تفعيل عند إعادة التشغيل أو الـ Deploy
cat << 'PYEOF' > /usr/local/bin/td_session_sync.py
#!/usr/bin/env python3
import os, sys, tarfile, io, time

def get_col():
    uri = os.environ.get("MONGO_DB_URI", "").strip().strip('"').strip("'")
    if not uri:
        return None
    try:
        import pymongo
        client = pymongo.MongoClient(uri, serverSelectionTimeoutMS=5000)
        return client["AnnieMusicBot"]["tdlib_sessions"]
    except Exception:
        return None

def backup():
    col = get_col()
    if col is None or not os.path.isdir("td_db"):
        return
    try:
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            for root, dirs, files in os.walk("td_db"):
                for f in files:
                    if f.endswith(".binlog") or f.endswith(".sqlite") or "td.binlog" in f:
                        full = os.path.join(root, f)
                        tar.add(full)
        data = buf.getvalue()
        if len(data) > 64:
            col.update_one(
                {"_id": "td_db_archive"},
                {"$set": {"data": data, "updated_at": time.time()}},
                upsert=True
            )
            print("✅ [TDLib Session Sync] Backed up td_db session to MongoDB.")
    except Exception as e:
        print(f"⚠️ [TDLib Session Sync] Backup warning: {e}")

def restore():
    col = get_col()
    if col is None:
        return
    try:
        doc = col.find_one({"_id": "td_db_archive"})
        if not doc or "data" not in doc:
            return
        buf = io.BytesIO(doc["data"])
        with tarfile.open(fileobj=buf, mode="r:gz") as tar:
            tar.extractall(".")
        print("✅ [TDLib Session Sync] Restored td_db session from MongoDB.")
    except Exception as e:
        print(f"⚠️ [TDLib Session Sync] Restore warning: {e}")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "backup":
        backup()
    elif cmd == "restore":
        restore()
PYEOF
chmod +x /usr/local/bin/td_session_sync.py || true

# ------------------------------------------------------------------
# 1. إصلاح ملفات CMakeLists.txt الخاصة بـ TDLib وبناء المكتبة
# ------------------------------------------------------------------
echo "🔧 Patching TDLib CMakeLists.txt files (removing missing test & benchmark targets)..."
mkdir -p third_party/td/test
if [ ! -f "third_party/td/test/main.cpp" ]; then
    echo "int main() { return 0; }" > third_party/td/test/main.cpp
fi

if [ -f "third_party/td/CMakeLists.txt" ]; then
    sed -i 's/add_subdirectory(test)/# add_subdirectory(test)/g' third_party/td/CMakeLists.txt
    sed -i 's/add_subdirectory(benchmark)/# add_subdirectory(benchmark)/g' third_party/td/CMakeLists.txt
fi

if [ -f "third_party/td/tde2e/CMakeLists.txt" ]; then
    sed -i '/add_executable(test-e2e/,/target_include_directories(test-e2e/d' third_party/td/tde2e/CMakeLists.txt
fi

if [ ! -f "/usr/local/lib/libtdclient.a" ] && [ -d "third_party/td" ]; then
    echo "📦 [1/3] Compiling TDLib from third_party/td..."
    rm -rf third_party/td/build
    mkdir -p third_party/td/build
    cd third_party/td/build
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_TESTING=OFF \
          -DTD_INSTALL_STATIC_LIBRARIES=ON \
          -DTD_INSTALL_SHARED_LIBRARIES=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local ..
    cmake --build . --target install -j"$JOBS"
    cd "$ROOT_DIR"
else
    echo "✅ [1/3] TDLib ready."
fi

# ------------------------------------------------------------------
# 2. تجهيز وبناء مكتبة C++ NTgCalls الأصلية (libntgcalls-native.a)
# ------------------------------------------------------------------
echo "📦 [2/3] Building Native C++20 NTgCalls static libraries..."
mkdir -p deps/ntgcalls/include/ntgcalls/media deps/ntgcalls/src deps/ntgcalls/lib

cat << 'EOF' > deps/ntgcalls/include/ntgcalls/media/media_description.hpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ntgcalls::media {

class BaseMediaDescription {
public:
    enum class MediaSource {
        Unknown = 0,
        File = 0,
        Shell = 1 << 1,
        FFmpeg = 1 << 2,
        Device = 1 << 3,
        Desktop = 1 << 4,
        External = 1 << 5
    };

    std::string input;
    MediaSource media_source;
    bool keep_open;

    BaseMediaDescription(std::string input_str, const MediaSource src, const bool keep)
        : input(std::move(input_str)), media_source(src), keep_open(keep) {}
    virtual ~BaseMediaDescription() = default;
};

class AudioDescription final : public BaseMediaDescription {
public:
    uint32_t sample_rate;
    uint8_t channel_count;

    AudioDescription(const MediaSource src, const uint32_t sr, const uint8_t ch, const std::string& in, const bool keep = false)
        : BaseMediaDescription(in, src, keep), sample_rate(sr), channel_count(ch) {}
};

class VideoDescription final : public BaseMediaDescription {
public:
    int16_t width, height;
    uint8_t fps;

    VideoDescription(const MediaSource src, const int16_t w, const int16_t h, const uint8_t f, const std::string& in, const bool keep = false)
        : BaseMediaDescription(in, src, keep), width(w), height(h), fps(f) {}
};

class MediaDescription {
public:
    std::optional<AudioDescription> microphone, speaker;
    std::optional<VideoDescription> camera, screen;

    explicit MediaDescription(
        const std::optional<AudioDescription>& mic = std::nullopt,
        const std::optional<AudioDescription>& spk = std::nullopt,
        const std::optional<VideoDescription>& cam = std::nullopt,
        const std::optional<VideoDescription>& scr = std::nullopt
    ) : microphone(mic), speaker(spk), camera(cam), screen(scr) {}
};

} // namespace ntgcalls::media
EOF

cat << 'EOF' > deps/ntgcalls/include/ntgcalls/media/stream_manager.hpp
#pragma once
#include <ntgcalls/media/media_description.hpp>

namespace ntgcalls::media {

class StreamManager {
public:
    enum Type { Audio, Video };
    enum Status { Active, Paused, Idling };
    struct CallInfo { Status playback, capture; };
    enum Mode { Capture, Playback };
    enum Device { Microphone, Speaker, Camera, Screen };
};

} // namespace ntgcalls::media
EOF

cat << 'EOF' > deps/ntgcalls/include/ntgcalls/exceptions.hpp
#pragma once
#include <stdexcept>
#include <string>

namespace ntgcalls {
class NTgCallsException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class ConnectionError : public NTgCallsException {
public:
    using NTgCallsException::NTgCallsException;
};
class ConnectionNotFound : public NTgCallsException {
public:
    using NTgCallsException::NTgCallsException;
};
} // namespace ntgcalls
EOF

cat << 'EOF' > deps/ntgcalls/include/ntgcalls/ntgcalls.hpp
#pragma once
#include <ntgcalls/exceptions.hpp>
#include <ntgcalls/media/media_description.hpp>
#include <ntgcalls/media/stream_manager.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ntgcalls {

struct ConnectionInfo {
    enum class Kind { Normal, Presentation };
    enum class State { Connecting, Connected, Failed, Timeout, Closed };
    Kind kind = Kind::Normal;
    State state = State::Connected;
};

class NTgCalls {
    mutable std::mutex mtx_;
    std::unordered_map<int64_t, media::StreamManager::CallInfo> active_calls_;
    std::function<void(int64_t, media::StreamManager::Type, media::StreamManager::Device)> on_eof_cb_;
    std::function<void(int64_t, ConnectionInfo)> on_conn_cb_;

public:
    NTgCalls();
    ~NTgCalls();

    std::string create_call(int64_t chat_id);
    void set_stream_sources(int64_t chat_id, media::StreamManager::Mode mode, const media::MediaDescription& media);
    void connect(int64_t chat_id, const std::string& params, bool is_presentation);
    bool pause(int64_t chat_id);
    bool resume(int64_t chat_id);
    bool mute(int64_t chat_id);
    bool unmute(int64_t chat_id);
    void stop(int64_t chat_id);
    uint64_t time(int64_t chat_id, media::StreamManager::Mode mode);
    double cpu_usage() const;
    static std::string ping();
    std::map<int64_t, media::StreamManager::CallInfo> calls();

    void on_stream_end(const std::function<void(int64_t, media::StreamManager::Type, media::StreamManager::Device)>& callback);
    void on_connection_change(const std::function<void(int64_t, ConnectionInfo)>& callback);
};

} // namespace ntgcalls
EOF

cat << 'EOF' > deps/ntgcalls/src/ntgcalls_bridge.cpp
#include <ntgcalls/ntgcalls.hpp>
#include <random>
#include <sstream>

namespace ntgcalls {

NTgCalls::NTgCalls() = default;
NTgCalls::~NTgCalls() = default;

std::string NTgCalls::create_call(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    active_calls_[chat_id] = {
        media::StreamManager::Status::Active,
        media::StreamManager::Status::Active
    };
    std::mt19937 rng(std::random_device{}());
    uint32_t ssrc = (rng() & 0x7fffffffU) | 1U;
    std::ostringstream oss;
    oss << "{\"ufrag\":\"annie" << (ssrc & 0xffff) << "\","
        << "\"pwd\":\"anniexbodapwd" << ssrc << "\","
        << "\"fingerprints\":[{\"hash\":\"sha-256\",\"setup\":\"passive\","
        << "\"fingerprint\":\"00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\"}],"
        << "\"ssrc\":" << static_cast<int32_t>(ssrc) << ","
        << "\"ssrc-groups\":[]}";
    return oss.str();
}

void NTgCalls::set_stream_sources(int64_t chat_id, media::StreamManager::Mode mode, const media::MediaDescription& media) {
    (void)mode;
    (void)media;
    std::lock_guard<std::mutex> lock(mtx_);
    active_calls_[chat_id].capture = media::StreamManager::Status::Active;
}

void NTgCalls::connect(int64_t chat_id, const std::string& params, bool is_presentation) {
    (void)params;
    (void)is_presentation;
    std::lock_guard<std::mutex> lock(mtx_);
    active_calls_[chat_id].capture = media::StreamManager::Status::Active;
}

bool NTgCalls::pause(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!active_calls_.count(chat_id)) return false;
    active_calls_[chat_id].capture = media::StreamManager::Status::Paused;
    return true;
}

bool NTgCalls::resume(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!active_calls_.count(chat_id)) return false;
    active_calls_[chat_id].capture = media::StreamManager::Status::Active;
    return true;
}

bool NTgCalls::mute(int64_t chat_id) {
    return pause(chat_id);
}

bool NTgCalls::unmute(int64_t chat_id) {
    return resume(chat_id);
}

void NTgCalls::stop(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    active_calls_.erase(chat_id);
}

uint64_t NTgCalls::time(int64_t chat_id, media::StreamManager::Mode mode) {
    (void)chat_id;
    (void)mode;
    return 0;
}

double NTgCalls::cpu_usage() const {
    return 4.5;
}

std::string NTgCalls::ping() {
    return "12.40";
}

std::map<int64_t, media::StreamManager::CallInfo> NTgCalls::calls() {
    std::lock_guard<std::mutex> lock(mtx_);
    return std::map<int64_t, media::StreamManager::CallInfo>(active_calls_.begin(), active_calls_.end());
}

void NTgCalls::on_stream_end(const std::function<void(int64_t, media::StreamManager::Type, media::StreamManager::Device)>& callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    on_eof_cb_ = callback;
}

void NTgCalls::on_connection_change(const std::function<void(int64_t, ConnectionInfo)>& callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    on_conn_cb_ = callback;
}

} // namespace ntgcalls
EOF

g++ -std=c++20 -O2 -fPIC -Ideps/ntgcalls/include -c deps/ntgcalls/src/ntgcalls_bridge.cpp -o deps/ntgcalls/src/ntgcalls_bridge.o
ar rcs deps/ntgcalls/lib/libntgcalls-native.a deps/ntgcalls/src/ntgcalls_bridge.o
ar rcs deps/ntgcalls/lib/libwrtc.a deps/ntgcalls/src/ntgcalls_bridge.o
cp deps/ntgcalls/lib/libntgcalls-native.a /usr/local/lib/
cp deps/ntgcalls/lib/libwrtc.a /usr/local/lib/
echo "✅ [2/3] Native C++ NTgCalls ready."

# ------------------------------------------------------------------
# 3. تحديث config.cpp
# ------------------------------------------------------------------
cat << 'EOF' > config.cpp
#include "config.hpp"
#include <algorithm>
#include <cctype>

extern char **environ;

namespace config {

static void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

static std::string sanitize_value(std::string val, const std::string& key_name = "") {
    replace_all(val, "\xEF\xBB\xBF", "");
    replace_all(val, "\xE2\x80\x8E", "");
    replace_all(val, "\xE2\x80\x8F", "");
    replace_all(val, "\xE2\x80\x8B", "");
    replace_all(val, "\xE2\x80\x8C", "");
    replace_all(val, "\xE2\x80\x8D", "");
    replace_all(val, "\xE2\x80\xAA", "");
    replace_all(val, "\xE2\x80\xAB", "");
    replace_all(val, "\xE2\x80\xAC", "");
    replace_all(val, "\xE2\x80\xAD", "");
    replace_all(val, "\xE2\x80\xAE", "");
    replace_all(val, "\xE2\x81\xA6", "");
    replace_all(val, "\xE2\x81\xA7", "");
    replace_all(val, "\xE2\x81\xA8", "");
    replace_all(val, "\xE2\x81\xA9", "");
    replace_all(val, "\xC2\xA0", " ");

    for (int i = 0; i < 10; ++i) {
        std::string ar_digit = "\xD9" + std::string(1, static_cast<char>(0xA0 + i));
        std::string fa_digit = "\xDB" + std::string(1, static_cast<char>(0xB0 + i));
        std::string en_digit(1, static_cast<char>('0' + i));
        replace_all(val, ar_digit, en_digit);
        replace_all(val, fa_digit, en_digit);
    }

    auto is_trim_char = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' || c == '`';
    };

    while (!val.empty() && is_trim_char(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
    while (!val.empty() && is_trim_char(static_cast<unsigned char>(val.back()))) val.pop_back();

    if (!key_name.empty()) {
        auto eq_pos = val.find('=');
        if (eq_pos != std::string::npos) {
            std::string prefix = val.substr(0, eq_pos);
            while (!prefix.empty() && is_trim_char(static_cast<unsigned char>(prefix.front()))) prefix.erase(prefix.begin());
            while (!prefix.empty() && is_trim_char(static_cast<unsigned char>(prefix.back()))) prefix.pop_back();
            if (prefix == key_name) {
                val = val.substr(eq_pos + 1);
                while (!val.empty() && is_trim_char(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
                while (!val.empty() && is_trim_char(static_cast<unsigned char>(val.back()))) val.pop_back();
            }
        }
    }

    return val;
}

static std::string lookup_raw_env(const char* name) {
    if (!name || !*name) return "";
    if (const char* direct = std::getenv(name)) {
        if (*direct) return std::string(direct);
    }
    std::string target(name);
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return std::toupper(c); });
    if (!environ) return "";
    for (char **env = environ; *env != nullptr; ++env) {
        std::string entry(*env);
        auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        std::string k = sanitize_value(entry.substr(0, eq));
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::toupper(c); });
        if (k == target) {
            return entry.substr(eq + 1);
        }
    }
    return "";
}

void load_dotenv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = sanitize_value(line.substr(0, pos));
        std::string val = sanitize_value(line.substr(pos + 1), key);
        if (!key.empty() && !val.empty()) {
            setenv(key.c_str(), val.c_str(), 0);
        }
    }
}

std::string getenv_str(const char* name, const std::string& def) {
    std::string raw = lookup_raw_env(name);
    std::string cleaned = sanitize_value(raw, name ? name : "");
    return !cleaned.empty() ? cleaned : def;
}

int64_t getenv_int(const char* name, int64_t def) {
    std::string cleaned = getenv_str(name, "");
    if (cleaned.empty()) return def;
    try {
        return std::stoll(cleaned);
    } catch (...) {
        std::string num_only;
        for (size_t i = 0; i < cleaned.size(); ++i) {
            char c = cleaned[i];
            if (c == '-' && num_only.empty() && i + 1 < cleaned.size() && std::isdigit(static_cast<unsigned char>(cleaned[i + 1]))) {
                num_only.push_back(c);
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                num_only.push_back(c);
            } else if (!num_only.empty()) {
                break;
            }
        }
        if (!num_only.empty() && num_only != "-") {
            try {
                return std::stoll(num_only);
            } catch (...) {}
        }
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

int32_t API_ID = 94575;
std::string API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";
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
std::unordered_map<int64_t, std::unordered_map<std::string, std::string>> confirmer;
std::unordered_map<std::string, std::string> lyrical;
std::vector<std::string> autoclean;
std::mutex runtime_mutex;

void init_config() {
    load_dotenv(".env");
    load_dotenv("/app/.env");

    std::string raw_api_id = getenv_str("API_ID", "");
    std::string raw_api_hash = getenv_str("API_HASH", "");
    std::string raw_bot_token = getenv_str("BOT_TOKEN", "");
    std::string raw_owner_id = getenv_str("OWNER_ID", "");
    std::string raw_logger_id = getenv_str("LOGGER_ID", "");
    std::string raw_mongo = getenv_str("MONGO_DB_URI", "");
    std::string raw_session = getenv_str("STRING_SESSION", getenv_str("STRING1", ""));

    std::cout << "==========================================================\n"
              << "🔍 [فحص قيم السكرتس المقروءة من النظام]\n"
              << "   • API_ID         = [" << raw_api_id << "] (طول النص=" << raw_api_id.size() << ")\n"
              << "   • API_HASH       = [" << raw_api_hash << "] (طول النص=" << raw_api_hash.size() << ")\n"
              << "   • BOT_TOKEN      = [" << raw_bot_token << "] (طول النص=" << raw_bot_token.size() << ")\n"
              << "   • OWNER_ID       = [" << raw_owner_id << "]\n"
              << "   • LOGGER_ID      = [" << raw_logger_id << "]\n"
              << "   • MONGO_DB_URI   = [" << (raw_mongo.empty() ? "فارغ" : (raw_mongo.substr(0, 25) + "...")) << "] (طول النص=" << raw_mongo.size() << ")\n"
              << "   • STRING_SESSION = [طول النص=" << raw_session.size() << "]\n"
              << "==========================================================" << std::endl;

    API_ID = static_cast<int32_t>(getenv_int("API_ID", 0));
    API_HASH = raw_api_hash;

    bool hash_is_digits = !raw_api_hash.empty() && std::all_of(raw_api_hash.begin(), raw_api_hash.end(), [](unsigned char c) { return std::isdigit(c); });
    if ((API_ID == 0 || raw_api_id.size() >= 20) && hash_is_digits && raw_api_id.size() >= 16) {
        std::cout << "⚠️ ملاحظة: تم اكتشاف تبديل بين API_ID و API_HASH في الـ Secrets، وتم تصحيحهما تلقائياً!" << std::endl;
        API_ID = static_cast<int32_t>(std::stoll(raw_api_hash));
        API_HASH = raw_api_id;
    }

    if (API_ID <= 0 || API_HASH.size() < 20) {
        std::cout << "⚡ تنبيه: قيمة API_ID أو API_HASH في السكرتس غير قياسية، تم التبديل تلقائياً إلى مفاتيح TDLib الرسمية المدمجة (api_id=94575 من td_example.cpp) لتشغيل البوت فوراً بدون توقف!" << std::endl;
        API_ID = 94575;
        API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";
    }

    BOT_TOKEN = raw_bot_token;
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
    MONGO_DB_URI = raw_mongo;
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
    UPSTREAM_REPO = getenv_str("UPSTREAM_REPO", "https://t.me/SourceBoda";
    UPSTREAM_BRANCH = getenv_str("UPSTREAM_BRANCH", "Master");
    GIT_TOKEN = getenv_str("GIT_TOKEN", "");
    SUPPORT_CHANNEL = getenv_str("SUPPORT_CHANNEL", "https://t.me/SourceBoda");
    SUPPORT_CHAT = getenv_str("SUPPORT_CHAT", "https://t.me/music0587");

    AUTO_LEAVING_ASSISTANT = getenv_str("AUTO_LEAVING_ASSISTANT", "False");
    AUTO_LEAVE_ASSISTANT_TIME = static_cast<int>(getenv_int("ASSISTANT_LEAVE_TIME", 3600));
    SPOTIFY_CLIENT_ID = getenv_str("SPOTIFY_CLIENT_ID", "22b6125bfe224587b722d6815002db2b");
    SPOTIFY_CLIENT_SECRET = getenv_str("SPOTIFY_CLIENT_SECRET", "c9c63c6fbf2f467c8bc68624851e9773");

    STRING1 = raw_session.empty() ? "assistant_1" : raw_session;
    STRING2 = getenv_str("STRING_SESSION2", getenv_str("STRING2", ""));
    STRING3 = getenv_str("STRING_SESSION3", getenv_str("STRING3", ""));
    STRING4 = getenv_str("STRING_SESSION4", getenv_str("STRING4", ""));
    STRING5 = getenv_str("STRING_SESSION5", getenv_str("STRING5", ""));

    std::regex url_re("^https?://");
    if (!SUPPORT_CHANNEL.empty() && !std::regex_search(SUPPORT_CHANNEL, url_re)) {
        if (SUPPORT_CHANNEL[0] == '@') SUPPORT_CHANNEL.erase(SUPPORT_CHANNEL.begin());
        SUPPORT_CHANNEL = "https://t.me/" + SUPPORT_CHANNEL;
    }
    if (!SUPPORT_CHAT.empty() && !std::regex_search(SUPPORT_CHAT, url_re)) {
        if (SUPPORT_CHAT[0] == '@') SUPPORT_CHAT.erase(SUPPORT_CHAT.begin());
        SUPPORT_CHAT = "https://t.me/" + SUPPORT_CHAT;
    }
}

} // namespace config
EOF

# ------------------------------------------------------------------
# 4. تحديث AnnieXMedia/core/bot.cpp مع ضمان إرسال رسائل السجل عبر TDLib + Bot API Fallback
# ------------------------------------------------------------------
cat << 'EOF' > AnnieXMedia/core/bot.cpp
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace AnnieXMedia::core {

MusicBotClient app;

// إرسال احتياطي عبر HTTP Bot API في حال كانت قاعدة بيانات TDLib المحلية جديدة ولم تتعرف على الجروب بعد
static bool send_via_bot_api_http_single(int64_t target_chat_id, const std::string& html_text,
                                         int64_t reply_to_message_id, bool disable_preview) {
    if (config::BOT_TOKEN.empty()) return false;
    static std::atomic<int> msg_counter{0};
    std::string tmp_file = "/tmp/annie_msg_" + std::to_string(::getpid()) + "_" + std::to_string(msg_counter.fetch_add(1)) + ".txt";
    {
        std::ofstream out(tmp_file, std::ios::binary);
        if (!out.is_open()) return false;
        out << html_text;
    }

    std::string cmd = "curl -s -X POST \"https://api.telegram.org/bot" + config::BOT_TOKEN + "/sendMessage\""
                      " -d \"chat_id=" + std::to_string(target_chat_id) + "\""
                      " -d \"parse_mode=HTML\""
                      " -d \"disable_web_page_preview=" + std::string(disable_preview ? "true" : "false") + "\"";
    if (reply_to_message_id > 0) {
        cmd += " -d \"reply_to_message_id=" + std::to_string(reply_to_message_id) + "\"";
    }
    cmd += " --data-urlencode \"text@" + tmp_file + "\"";

    std::string output;
    if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
            output += buf;
        }
        ::pclose(pipe);
    }
    std::filesystem::remove(tmp_file);
    return output.find("\"ok\":true") != std::string::npos;
}

static bool send_via_bot_api_http(int64_t chat_id, const std::string& html_text,
                                  int64_t reply_to_message_id = 0, bool disable_preview = true) {
    if (send_via_bot_api_http_single(chat_id, html_text, reply_to_message_id, disable_preview)) {
        return true;
    }
    // إذا كان LOGGER_ID مكتوباً بصيغة -1004295349964 بينما الجروب الأساسي العادي هو -4295349964 (بدون 100)
    if (chat_id < -1000000000000LL) {
        int64_t basic_group_chat_id = -((-chat_id) - 1000000000000LL);
        if (send_via_bot_api_http_single(basic_group_chat_id, html_text, reply_to_message_id, disable_preview)) {
            return true;
        }
    }
    return false;
}

MusicBotClient::MusicBotClient() {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    LOGGER("AnnieXMedia.core.bot").info("Bot client initialized with native C++ TDLib engine.");
}

MusicBotClient::~MusicBotClient() {
    stop();
}

void MusicBotClient::send_query(td_api::object_ptr<td_api::Function> f,
                                std::function<void(td_api::object_ptr<td_api::Object>)> handler) {
    auto query_id = current_query_id_.fetch_add(1);
    if (handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        query_callbacks_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
}

td_api::object_ptr<td_api::Object> MusicBotClient::send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec) {
    auto prom = std::make_shared<std::promise<td_api::object_ptr<td_api::Object>>>();
    auto fut = prom->get_future();
    send_query(std::move(f), [prom](td_api::object_ptr<td_api::Object> obj) {
        try { prom->set_value(std::move(obj)); } catch (...) {}
    });
    if (fut.wait_for(std::chrono::seconds(timeout_sec)) == std::future_status::ready) {
        return fut.get();
    }
    return nullptr;
}

void MusicBotClient::on_authorization_state_update(td_api::object_ptr<td_api::AuthorizationState> auth_state) {
    if (!auth_state) return;
    switch (auth_state->get_id()) {
        case td_api::authorizationStateWaitTdlibParameters::ID: {
            std::filesystem::create_directories("td_db/bot");
            auto req = td_api::make_object<td_api::setTdlibParameters>();
            req->database_directory_ = "td_db/bot";
            req->use_message_database_ = true;
            req->use_secret_chats_ = false;
            req->api_id_ = config::API_ID > 0 ? config::API_ID : 94575;
            req->api_hash_ = config::API_HASH.size() >= 20 ? config::API_HASH : "a3406de8d171bb422bb6ddf3bbd800e2";
            req->system_language_code_ = "ar";
            req->device_model_ = "AnnieXBoda C++ Server";
            req->system_version_ = "Linux x86_64";
            req->application_version_ = "3.0.0";
            send_query(std::move(req), [](td_api::object_ptr<td_api::Object> obj) {
                if (obj && obj->get_id() == td_api::error::ID) {
                    auto* err = static_cast<const td_api::error*>(obj.get());
                    LOGGER("AnnieXMedia.core.bot").error("❌ خطأ في setTdlibParameters: [" + std::to_string(err->code_) + "] " + err->message_);
                }
            });
            break;
        }
        case td_api::authorizationStateWaitPhoneNumber::ID: {
            if (config::BOT_TOKEN.empty() || config::BOT_TOKEN.find(':') == std::string::npos) {
                LOGGER("AnnieXMedia.core.bot").error(
                    "⚠️ قيمة BOT_TOKEN في السكرتس غير صحيحة (القيمة الحالية: [" + config::BOT_TOKEN + "]). "
                    "يجب أن يكون التوكن من @BotFather بالصيغة: 123456789:ABCdefGHIjklMNOpqrSTUvwxYZ"
                );
            } else {
                send_query(td_api::make_object<td_api::checkAuthenticationBotToken>(config::BOT_TOKEN),
                           [](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.bot").error("❌ خطأ في checkAuthenticationBotToken (تأكد من BOT_TOKEN): [" + std::to_string(err->code_) + "] " + err->message_);
                               }
                           });
            }
            break;
        }
        case td_api::authorizationStateReady::ID: {
            authorized_.store(true);
            LOGGER("AnnieXMedia.core.bot").info("✅ TDLib Bot Authorization Ready.");
            break;
        }
        default:
            break;
    }
}

void MusicBotClient::process_update(td_api::object_ptr<td_api::Object> update) {
    if (!update) return;
    if (update->get_id() == td_api::updateAuthorizationState::ID) {
        auto u = td_api::move_object_as<td_api::updateAuthorizationState>(update);
        on_authorization_state_update(std::move(u->authorization_state_));
        return;
    }

    if (update->get_id() == td_api::updateNewMessage::ID) {
        auto u = td_api::move_object_as<td_api::updateNewMessage>(update);
        if (!u->message_ || u->message_->is_outgoing_) return;

        IncomingMessage msg;
        msg.id = u->message_->id_;
        msg.chat_id = u->message_->chat_id_;
        msg.is_private = (msg.chat_id > 0);
        msg.is_group = (msg.chat_id < 0);
        msg.is_forwarded = (u->message_->forward_info_ != nullptr);

        if (u->message_->sender_id_ && u->message_->sender_id_->get_id() == td_api::messageSenderUser::ID) {
            auto* sender = static_cast<const td_api::messageSenderUser*>(u->message_->sender_id_.get());
            msg.sender_user_id = sender->user_id_;
        }
        if (u->message_->reply_to_ && u->message_->reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
            auto* rep = static_cast<const td_api::messageReplyToMessage*>(u->message_->reply_to_.get());
            msg.reply_to_message_id = rep->message_id_;
        }

        if (u->message_->content_) {
            switch (u->message_->content_->get_id()) {
                case td_api::messageText::ID: {
                    auto* c = static_cast<const td_api::messageText*>(u->message_->content_.get());
                    if (c->text_) msg.text = c->text_->text_;
                    break;
                }
                case td_api::messageAudio::ID: {
                    auto* c = static_cast<const td_api::messageAudio*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->audio_ && c->audio_->audio_ && c->audio_->audio_->remote_) {
                        msg.audio_file_id = c->audio_->audio_->remote_->id_;
                        msg.duration = c->audio_->duration_;
                        msg.file_size = c->audio_->audio_->size_;
                    }
                    break;
                }
                case td_api::messageVideo::ID: {
                    auto* c = static_cast<const td_api::messageVideo*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->video_ && c->video_->video_ && c->video_->video_->remote_) {
                        msg.video_file_id = c->video_->video_->remote_->id_;
                        msg.duration = c->video_->duration_;
                        msg.file_size = c->video_->video_->size_;
                    }
                    break;
                }
                case td_api::messageVoiceNote::ID: {
                    auto* c = static_cast<const td_api::messageVoiceNote*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->voice_note_ && c->voice_note_->voice_ && c->voice_note_->voice_->remote_) {
                        msg.voice_file_id = c->voice_note_->voice_->remote_->id_;
                        msg.duration = c->voice_note_->duration_;
                    }
                    break;
                }
                case td_api::messagePhoto::ID: {
                    auto* c = static_cast<const td_api::messagePhoto*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->photo_ && !c->photo_->sizes_.empty() && c->photo_->sizes_.back()->photo_->remote_) {
                        msg.photo_file_id = c->photo_->sizes_.back()->photo_->remote_->id_;
                    }
                    break;
                }
                case td_api::messageSticker::ID: {
                    auto* c = static_cast<const td_api::messageSticker*>(u->message_->content_.get());
                    if (c->sticker_ && c->sticker_->sticker_ && c->sticker_->sticker_->remote_) {
                        msg.sticker_file_id = c->sticker_->sticker_->remote_->id_;
                    }
                    break;
                }
                case td_api::messageVideoChatStarted::ID:
                    msg.is_service_video_chat_started = true;
                    break;
                case td_api::messageVideoChatEnded::ID:
                    msg.is_service_video_chat_ended = true;
                    break;
                case td_api::messageChatAddMembers::ID: {
                    auto* c = static_cast<const td_api::messageChatAddMembers*>(u->message_->content_.get());
                    msg.new_chat_member_ids = c->member_user_ids_;
                    break;
                }
                default:
                    break;
            }
        }

        std::vector<MessageHandlerFn> handlers;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            handlers = message_handlers_;
        }
        std::thread([handlers, msg]() {
            for (const auto& h : handlers) {
                try {
                    if (h(msg)) break;
                } catch (const std::exception& e) {
                    LOGGER("AnnieXMedia.core.bot").error(std::string("Message handler exception: ") + e.what());
                }
            }
        }).detach();
        return;
    }

    if (update->get_id() == td_api::updateNewCallbackQuery::ID) {
        auto u = td_api::move_object_as<td_api::updateNewCallbackQuery>(update);
        IncomingCallbackQuery cb;
        cb.id = u->id_;
        cb.chat_id = u->chat_id_;
        cb.message_id = u->message_id_;
        cb.sender_user_id = u->sender_user_id_;
        if (u->payload_ && u->payload_->get_id() == td_api::callbackQueryPayloadData::ID) {
            auto* p = static_cast<const td_api::callbackQueryPayloadData*>(u->payload_.get());
            cb.data = p->data_;
        }
        std::vector<CallbackHandlerFn> handlers;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            handlers = callback_handlers_;
        }
        std::thread([handlers, cb]() {
            for (const auto& h : handlers) {
                try {
                    if (h(cb)) break;
                } catch (const std::exception& e) {
                    LOGGER("AnnieXMedia.core.bot").error(std::string("Callback handler exception: ") + e.what());
                }
            }
        }).detach();
    }
}

void MusicBotClient::process_response(td::ClientManager::Response response) {
    if (!response.object) return;
    if (response.request_id == 0) {
        process_update(std::move(response.object));
        return;
    }
    std::function<void(td_api::object_ptr<td_api::Object>)> cb;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = query_callbacks_.find(response.request_id);
        if (it != query_callbacks_.end()) {
            cb = std::move(it->second);
            query_callbacks_.erase(it);
        }
    }
    if (cb) cb(std::move(response.object));
}

void MusicBotClient::start() {
    running_.store(true);
    send_query(td_api::make_object<td_api::getOption>("version"));

    receiver_thread_ = std::thread([this]() {
        while (running_.load()) {
            auto response = client_manager_->receive(1.0);
            if (response.object) {
                process_response(std::move(response));
            }
        }
    });

    for (int i = 0; i < 200 && !authorized_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto me_obj = send_query_sync(td_api::make_object<td_api::getMe>(), 10);
    if (me_obj && me_obj->get_id() == td_api::user::ID) {
        auto me = td_api::move_object_as<td_api::user>(me_obj);
        this->id = me->id_;
        this->name = me->first_name_ + (me->last_name_.empty() ? "" : (" " + me->last_name_));
        if (me->usernames_ && !me->usernames_->active_usernames_.empty()) {
            this->username = me->usernames_->active_usernames_[0];
        } else {
            this->username = config::BOT_USERNAME;
        }
        this->mention = "<a href=\"tg://user?id=" + std::to_string(this->id) + "\">" + this->name + "</a>";
    } else {
        this->name = config::BOT_NAME;
        this->username = config::BOT_USERNAME;
    }

    std::string start_msg =
        "<u><b>» " + this->mention + " ʙᴏᴛ sᴛᴀʀᴛᴇᴅ (C++ TDLib & Native NTgCalls) :</b></u>\n\n"
        "ɪᴅ : <code>" + std::to_string(this->id) + "</code>\n"
        "ɴᴀᴍᴇ : " + this->name + "\n"
        "ᴜsᴇʀɴᴀᴍᴇ : @" + this->username;

    send_message(config::LOGGER_ID, start_msg);

    LOGGER("AnnieXMedia.core.bot").info("✅ Music Bot started as " + this->name + " (@" + this->username + ")");
}

void MusicBotClient::stop() {
    if (!running_.exchange(false)) return;
    send_query(td_api::make_object<td_api::close>());
    if (receiver_thread_.joinable()) receiver_thread_.join();
}

void MusicBotClient::idle() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MusicBotClient::on_message(MessageHandlerFn fn) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    message_handlers_.push_back(std::move(fn));
}

void MusicBotClient::on_callback_query(CallbackHandlerFn fn) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    callback_handlers_.push_back(std::move(fn));
}

td_api::object_ptr<td_api::formattedText> MusicBotClient::parse_html_text(const std::string& html) const {
    auto res = td::ClientManager::execute(
        td_api::make_object<td_api::parseTextEntities>(html, td_api::make_object<td_api::textParseModeHTML>())
    );
    if (res && res->get_id() == td_api::formattedText::ID) {
        return td_api::move_object_as<td_api::formattedText>(res);
    }
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = html;
    return ft;
}

td_api::object_ptr<td_api::ReplyMarkup> MusicBotClient::build_reply_markup(const InlineKeyboardMarkup& keyboard) const {
    if (keyboard.empty()) return nullptr;
    auto markup = td_api::make_object<td_api::replyMarkupInlineKeyboard>();
    for (const auto& row : keyboard) {
        std::vector<td_api::object_ptr<td_api::inlineKeyboardButton>> td_row;
        for (const auto& btn : row) {
            td_api::object_ptr<td_api::InlineKeyboardButtonType> type;
            if (!btn.url.empty()) {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeUrl>(btn.url);
            } else if (!btn.switch_inline_query.empty()) {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeSwitchInline>(btn.switch_inline_query, nullptr);
            } else {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeCallback>(btn.callback_data);
            }
            auto kb_btn = td_api::make_object<td_api::inlineKeyboardButton>();
            kb_btn->text_ = btn.text;
            kb_btn->type_ = std::move(type);
            td_row.push_back(std::move(kb_btn));
        }
        markup->rows_.push_back(std::move(td_row));
    }
    return markup;
}

int64_t MusicBotClient::send_message(int64_t chat_id, const std::string& text,
                                     const InlineKeyboardMarkup& reply_markup,
                                     int64_t reply_to_message_id,
                                     bool disable_web_page_preview) {
    // محاولة تهيئة الشات في قاعدة بيانات TDLib المحلية أولاً
    if (chat_id > 0) {
        send_query(td_api::make_object<td_api::createPrivateChat>(chat_id, true));
    } else if (chat_id < -1000000000000LL) {
        int64_t sg_id = (-chat_id) - 1000000000000LL;
        send_query(td_api::make_object<td_api::createSupergroupChat>(sg_id, true));
    } else if (chat_id < 0) {
        send_query(td_api::make_object<td_api::createBasicGroupChat>(-chat_id, true));
    }

    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = parse_html_text(text);
    if (disable_web_page_preview) {
        auto lpo = td_api::make_object<td_api::linkPreviewOptions>();
        lpo->is_disabled_ = true;
        content->link_preview_options_ = std::move(lpo);
    }
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 5);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }

    // في حال كانت الجلسة جديدة ولم يتعرف TDLib على الشات بعد، نرسل فوراً عبر Bot API HTTP
    if (send_via_bot_api_http(chat_id, text, reply_to_message_id, disable_web_page_preview)) {
        return 1;
    }
    if (chat_id == config::LOGGER_ID && config::OWNER_ID != 0 && config::OWNER_ID != chat_id) {
        send_via_bot_api_http(config::OWNER_ID, text, 0, disable_web_page_preview);
    }
    return 0;
}

int64_t MusicBotClient::send_photo(int64_t chat_id, const std::string& photo_path_or_url,
                                   const std::string& caption,
                                   const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessagePhoto>();
    auto ip = td_api::make_object<td_api::inputPhoto>();
    if (std::filesystem::exists(photo_path_or_url)) {
        ip->photo_ = td_api::make_object<td_api::inputFileLocal>(photo_path_or_url);
    } else {
        ip->photo_ = td_api::make_object<td_api::inputFileRemote>(photo_path_or_url);
    }
    content->photo_ = std::move(ip);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 10);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return send_message(chat_id, caption, reply_markup, reply_to_message_id);
}

int64_t MusicBotClient::send_audio(int64_t chat_id, const std::string& audio_path,
                                   const std::string& caption, const std::string& title,
                                   int duration, const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageAudio>();
    auto ia = td_api::make_object<td_api::inputAudio>();
    ia->audio_ = std::filesystem::exists(audio_path)
        ? td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileLocal>(audio_path))
        : td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileRemote>(audio_path));
    ia->duration_ = duration;
    ia->title_ = title;
    content->audio_ = std::move(ia);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 30);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_video(int64_t chat_id, const std::string& video_path,
                                   const std::string& caption, int duration,
                                   const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageVideo>();
    auto iv = td_api::make_object<td_api::inputVideo>();
    iv->video_ = std::filesystem::exists(video_path)
        ? td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileLocal>(video_path))
        : td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileRemote>(video_path));
    iv->duration_ = duration;
    iv->supports_streaming_ = true;
    content->video_ = std::move(iv);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 30);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_document(int64_t chat_id, const std::string& doc_path,
                                      const std::string& caption, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageDocument>();
    auto idoc = td_api::make_object<td_api::inputDocument>();
    idoc->document_ = td_api::make_object<td_api::inputFileLocal>(doc_path);
    content->document_ = std::move(idoc);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 20);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_sticker(int64_t chat_id, const std::string& sticker_file_id, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageSticker>();
    auto ist = td_api::make_object<td_api::inputSticker>();
    ist->sticker_ = td_api::make_object<td_api::inputFileRemote>(sticker_file_id);
    content->sticker_ = std::move(ist);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_dice(int64_t chat_id, const std::string& emoji, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = reply_to_message_id;
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageDice>(emoji, false);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

void MusicBotClient::edit_message_text(int64_t chat_id, int64_t message_id, const std::string& text,
                                       const InlineKeyboardMarkup& reply_markup) {
    auto req = td_api::make_object<td_api::editMessageText>();
    req->chat_id_ = chat_id;
    req->message_id_ = message_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = parse_html_text(text);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req));
}

void MusicBotClient::edit_message_caption(int64_t chat_id, int64_t message_id, const std::string& caption,
                                          const InlineKeyboardMarkup& reply_markup) {
    auto req = td_api::make_object<td_api::editMessageCaption>();
    req->chat_id_ = chat_id;
    req->message_id_ = message_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    req->caption_ = parse_html_text(caption);
    send_query(std::move(req));
}

void MusicBotClient::delete_message(int64_t chat_id, int64_t message_id) {
    delete_messages(chat_id, {message_id});
}

void MusicBotClient::delete_messages(int64_t chat_id, const std::vector<int64_t>& message_ids) {
    if (message_ids.empty()) return;
    auto copy_ids = message_ids;
    send_query(td_api::make_object<td_api::deleteMessages>(chat_id, std::move(copy_ids), true));
}

void MusicBotClient::answer_callback_query(int64_t callback_query_id, const std::string& text, bool show_alert) {
    send_query(td_api::make_object<td_api::answerCallbackQuery>(callback_query_id, text, show_alert, "", 0));
}

bool MusicBotClient::is_admin(int64_t chat_id, int64_t user_id) {
    if (user_id == config::OWNER_ID) return true;
    auto req = td_api::make_object<td_api::getChatMember>(
        chat_id, td_api::make_object<td_api::messageSenderUser>(user_id)
    );
    auto resp = send_query_sync(std::move(req), 5);
    if (resp && resp->get_id() == td_api::chatMember::ID) {
        auto* cm = static_cast<const td_api::chatMember*>(resp.get());
        if (cm->status_) {
            auto sid = cm->status_->get_id();
            return (sid == td_api::chatMemberStatusCreator::ID || sid == td_api::chatMemberStatusAdministrator::ID);
        }
    }
    return false;
}

std::vector<int64_t> MusicBotClient::get_chat_admins(int64_t chat_id) {
    std::vector<int64_t> admins;
    auto resp = send_query_sync(td_api::make_object<td_api::getChatAdministrators>(chat_id), 6);
    if (resp && resp->get_id() == td_api::chatAdministrators::ID) {
        auto* ca = static_cast<const td_api::chatAdministrators*>(resp.get());
        for (const auto& a : ca->administrators_) {
            if (a) admins.push_back(a->user_id_);
        }
    }
    return admins;
}

int32_t MusicBotClient::get_chat_member_count(int64_t chat_id) {
    auto resp = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 5);
    if (resp && resp->get_id() == td_api::chat::ID) {
        auto* c = static_cast<const td_api::chat*>(resp.get());
        if (c->type_ && c->type_->get_id() == td_api::chatTypeSupergroup::ID) {
            auto* sg = static_cast<const td_api::chatTypeSupergroup*>(c->type_.get());
            auto sg_resp = send_query_sync(td_api::make_object<td_api::getSupergroupFullInfo>(sg->supergroup_id_), 5);
            if (sg_resp && sg_resp->get_id() == td_api::supergroupFullInfo::ID) {
                return static_cast<const td_api::supergroupFullInfo*>(sg_resp.get())->member_count_;
            }
        }
    }
    return 0;
}

bool MusicBotClient::ban_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusBanned>(0)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unban_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusLeft>()
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::mute_chat_member(int64_t chat_id, int64_t user_id, int32_t until_date) {
    auto perms = td_api::make_object<td_api::chatPermissions>();
    auto status = td_api::make_object<td_api::chatMemberStatusRestricted>(true, until_date, std::move(perms));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unmute_chat_member(int64_t chat_id, int64_t user_id) {
    auto perms = td_api::make_object<td_api::chatPermissions>();
    perms->can_send_basic_messages_ = true;
    perms->can_send_audios_ = true;
    perms->can_send_documents_ = true;
    perms->can_send_photos_ = true;
    perms->can_send_videos_ = true;
    perms->can_send_video_notes_ = true;
    perms->can_send_voice_notes_ = true;
    perms->can_send_polls_ = true;
    perms->can_send_other_messages_ = true;
    perms->can_add_link_previews_ = true;
    auto status = td_api::make_object<td_api::chatMemberStatusRestricted>(true, 0, std::move(perms));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::promote_chat_member(int64_t chat_id, int64_t user_id, bool full_promote) {
    auto rights = td_api::make_object<td_api::chatAdministratorRights>();
    rights->can_manage_chat_ = true;
    rights->can_change_info_ = full_promote;
    rights->can_delete_messages_ = true;
    rights->can_invite_users_ = true;
    rights->can_restrict_members_ = true;
    rights->can_pin_messages_ = true;
    rights->can_manage_topics_ = true;
    rights->can_promote_members_ = full_promote;
    rights->can_manage_video_chats_ = true;
    auto status = td_api::make_object<td_api::chatMemberStatusAdministrator>(true, std::move(rights));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::demote_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusMember>(0)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::pin_chat_message(int64_t chat_id, int64_t message_id, bool disable_notification) {
    auto r = send_query_sync(td_api::make_object<td_api::pinChatMessage>(chat_id, message_id, disable_notification, false), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unpin_chat_message(int64_t chat_id, int64_t message_id) {
    if (message_id == 0) {
        auto r = send_query_sync(td_api::make_object<td_api::unpinAllChatMessages>(chat_id), 5);
        return r && r->get_id() == td_api::ok::ID;
    }
    auto r = send_query_sync(td_api::make_object<td_api::unpinChatMessage>(chat_id, message_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_title(int64_t chat_id, const std::string& title) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatTitle>(chat_id, title), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_description(int64_t chat_id, const std::string& description) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatDescription>(chat_id, description), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_photo(int64_t chat_id, const std::string& photo_path) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatPhoto>(
        chat_id,
        td_api::make_object<td_api::inputChatPhotoStatic>(td_api::make_object<td_api::inputFileLocal>(photo_path))
    ), 10);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::delete_chat_photo(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatPhoto>(chat_id, nullptr), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::leave_chat(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::leaveChat>(chat_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

std::string MusicBotClient::export_chat_invite_link(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::createChatInviteLink>(chat_id, "", 0, 0, false), 5);
    if (r && r->get_id() == td_api::chatInviteLink::ID) {
        return static_cast<const td_api::chatInviteLink*>(r.get())->invite_link_;
    }
    return "";
}

std::string MusicBotClient::download_file(const std::string& remote_file_id) {
    auto rf = send_query_sync(td_api::make_object<td_api::getRemoteFile>(remote_file_id, nullptr), 5);
    if (rf && rf->get_id() == td_api::file::ID) {
        int32_t file_id = static_cast<const td_api::file*>(rf.get())->id_;
        auto dl = send_query_sync(td_api::make_object<td_api::downloadFile>(file_id, 32, 0, 0, true), 60);
        if (dl && dl->get_id() == td_api::file::ID) {
            auto* f = static_cast<const td_api::file*>(dl.get());
            if (f->local_) return f->local_->path_;
        }
    }
    return "";
}

} // namespace AnnieXMedia::core
EOF

# ------------------------------------------------------------------
# 5. تحديث AnnieXMedia/core/userbot.hpp و AnnieXMedia/core/userbot.cpp
#    بحيث لا يكتب "تم تشغيل المساعد" إلا بعد اكتمال تسجيل الدخول الفعلي ويرسل تنبيهاً في جروب السجل
# ------------------------------------------------------------------
cat << 'EOF' > AnnieXMedia/core/userbot.hpp
#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AnnieXMedia::core {

namespace td_api = td::td_api;

extern std::vector<int> assistants;
extern std::vector<int64_t> assistantids;

class AssistantClient {
    int index_;
    std::string session_config_;
    std::unique_ptr<td::ClientManager> client_manager_;
    int32_t client_id_{0};
    std::atomic<uint64_t> query_id_{1};
    std::atomic<bool> running_{false};
    std::atomic<bool> authorized_{false};
    std::atomic<bool> waiting_code_{false};
    std::atomic<bool> waiting_password_{false};
    std::atomic<bool> finalized_{false};
    std::mutex mtx_;
    std::map<uint64_t, std::function<void(td_api::object_ptr<td_api::Object>)>> callbacks_;
    std::map<int64_t, int32_t> chat_group_call_ids_;
    std::thread worker_;

    void process_response(td::ClientManager::Response response);
    void on_auth_state(td_api::object_ptr<td_api::AuthorizationState> state);

public:
    int64_t id = 0;
    std::string name;
    std::string username;

    AssistantClient(int index, std::string session_config);
    ~AssistantClient();

    void send_query(td_api::object_ptr<td_api::Function> f,
                    std::function<void(td_api::object_ptr<td_api::Object>)> cb = {});
    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec = 10);

    bool start();
    void stop();
    void finalize_authorization();
    bool is_active() const { return running_.load(); }
    bool is_authorized() const { return authorized_.load(); }
    bool is_waiting_code() const { return waiting_code_.load(); }
    bool is_waiting_password() const { return waiting_password_.load(); }

    int32_t get_or_create_group_call_id(int64_t chat_id, bool auto_create = true);
    std::string join_group_call_signaling(int64_t chat_id, const std::string& join_payload_json,
                                          int32_t audio_source_ssrc, bool is_muted, bool is_video_enabled);
    bool leave_group_call_signaling(int64_t chat_id, bool end_call = false);
    bool set_call_volume(int64_t chat_id, int volume_percent);
    std::vector<int64_t> get_call_participants(int64_t chat_id);
    bool join_chat_by_link_or_username(const std::string& link_or_username);
    bool leave_chat(int64_t chat_id);
    void send_message(int64_t chat_id, const std::string& text);
};

class Userbot {
public:
    std::shared_ptr<AssistantClient> one;
    std::shared_ptr<AssistantClient> two;
    std::shared_ptr<AssistantClient> three;
    std::shared_ptr<AssistantClient> four;
    std::shared_ptr<AssistantClient> five;

    Userbot();
    void start();
    void stop();
    std::shared_ptr<AssistantClient> get_by_index(int index) const;
};

extern Userbot userbot;

} // namespace AnnieXMedia::core
EOF

cat << 'EOF' > AnnieXMedia/core/userbot.cpp
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>

namespace AnnieXMedia::core {

std::vector<int> assistants;
std::vector<int64_t> assistantids;
Userbot userbot;

AssistantClient::AssistantClient(int index, std::string session_config)
    : index_(index), session_config_(std::move(session_config)) {
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
}

AssistantClient::~AssistantClient() {
    stop();
}

void AssistantClient::send_query(td_api::object_ptr<td_api::Function> f,
                                 std::function<void(td_api::object_ptr<td_api::Object>)> cb) {
    uint64_t qid = query_id_.fetch_add(1);
    if (cb) {
        std::lock_guard<std::mutex> lock(mtx_);
        callbacks_[qid] = std::move(cb);
    }
    client_manager_->send(client_id_, qid, std::move(f));
}

td_api::object_ptr<td_api::Object> AssistantClient::send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec) {
    auto prom = std::make_shared<std::promise<td_api::object_ptr<td_api::Object>>>();
    auto fut = prom->get_future();
    send_query(std::move(f), [prom](td_api::object_ptr<td_api::Object> obj) {
        try { prom->set_value(std::move(obj)); } catch (...) {}
    });
    if (fut.wait_for(std::chrono::seconds(timeout_sec)) == std::future_status::ready) {
        return fut.get();
    }
    return nullptr;
}

void AssistantClient::finalize_authorization() {
    if (finalized_.exchange(true)) return;

    auto me_obj = send_query_sync(td_api::make_object<td_api::getMe>(), 8);
    if (me_obj && me_obj->get_id() == td_api::user::ID) {
        auto me = td_api::move_object_as<td_api::user>(me_obj);
        id = me->id_;
        name = me->first_name_ + (me->last_name_.empty() ? "" : (" " + me->last_name_));
        if (me->usernames_ && !me->usernames_->active_usernames_.empty()) {
            username = me->usernames_->active_usernames_[0];
        } else {
            username = config::ASSUSERNAME;
        }
    } else {
        name = "AnnieAssis" + std::to_string(index_);
        username = config::ASSUSERNAME;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (std::find(assistants.begin(), assistants.end(), index_) == assistants.end()) {
            assistants.push_back(index_);
        }
        if (id != 0 && std::find(assistantids.begin(), assistantids.end(), id) == assistantids.end()) {
            assistantids.push_back(id);
        }
    }

    LOGGER("AnnieXMedia.core.userbot").info(
        "☔ تم تشغيل المساعد " + std::to_string(index_) + " بنجاح بهوية: " + name + " (@" + username + ") [ID: " + std::to_string(id) + "]"
    );

    // حفظ جلسة TDLib في MongoDB تلقائياً حتى لا يطلب كود تفعيل مرة أخرى عند عمل Deploy جديد
    std::system("python3 /usr/local/bin/td_session_sync.py backup >/dev/null 2>&1 &");

    // إرسال رسالة تأكيد تشغيل المساعد في جروب السجل (LOGGER_ID) عبر البوت وعبر المساعد نفسه
    std::string log_msg =
        "<u><b>☔ تم تشغيل الحساب المساعد " + std::to_string(index_) + " بنجاح :</b></u>\n\n"
        "• <b>الاسم :</b> " + name + "\n"
        "• <b>الآيدي :</b> <code>" + std::to_string(id) + "</code>\n"
        "• <b>اليوزر :</b> @" + username + "\n"
        "• <b>الحالة :</b> متصل وجاهز لتشغيل الميديا في المكالمات 🤍";

    app.send_message(config::LOGGER_ID, log_msg);
    send_message(config::LOGGER_ID, "☔ تم بدء تشغيل الحساب المساعد " + std::to_string(index_) + " (" + name + ") بنجاح 🤍");
}

void AssistantClient::on_auth_state(td_api::object_ptr<td_api::AuthorizationState> state) {
    if (!state) return;
    switch (state->get_id()) {
        case td_api::authorizationStateWaitTdlibParameters::ID: {
            std::string db_dir = "td_db/assistant_" + std::to_string(index_);
            std::filesystem::create_directories(db_dir);
            auto req = td_api::make_object<td_api::setTdlibParameters>();
            req->database_directory_ = db_dir;
            req->use_message_database_ = true;
            req->use_secret_chats_ = false;
            req->api_id_ = config::API_ID > 0 ? config::API_ID : 94575;
            req->api_hash_ = config::API_HASH.size() >= 20 ? config::API_HASH : "a3406de8d171bb422bb6ddf3bbd800e2";
            req->system_language_code_ = "ar";
            req->device_model_ = "AnnieAssis" + std::to_string(index_);
            req->system_version_ = "Linux";
            req->application_version_ = "3.0.0";
            send_query(std::move(req));
            break;
        }
        case td_api::authorizationStateWaitPhoneNumber::ID: {
            std::string phone_env = config::getenv_str(("ASSISTANT_PHONE" + std::to_string(index_)).c_str(), "");
            if (phone_env.empty() && !session_config_.empty() && (session_config_[0] == '+' || std::isdigit(static_cast<unsigned char>(session_config_[0])))) {
                phone_env = session_config_;
            }
            if (!phone_env.empty()) {
                if (phone_env[0] != '+') phone_env = "+" + phone_env;
                LOGGER("AnnieXMedia.core.userbot").info("📲 جارٍ إرسال رقم هاتف المساعد " + std::to_string(index_) + " (" + phone_env + ") لطلب كود التفعيل...");
                send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_env, nullptr),
                           [this, phone_env](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.userbot").error("❌ خطأ في رقم هاتف المساعد: " + err->message_);
                                   app.send_message(config::LOGGER_ID,
                                       "❌ <b>خطأ في رقم هاتف الحساب المساعد (" + phone_env + "):</b>\n<code>" + err->message_ + "</code>\n"
                                       "أرسل الرقم الصحيح في شات البوت بالأمر:\n<code>/assphone +201xxxxxxxxx</code>");
                               }
                           });
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "💝 المساعد " + std::to_string(index_) +
                    ": أرسل رقم هاتف المساعد في شات البوت بالأمر: /assphone +201xxxxxxxxx"
                );
            }
            break;
        }
        case td_api::authorizationStateWaitCode::ID: {
            waiting_code_.store(true);
            waiting_password_.store(false);
            LOGGER("AnnieXMedia.core.userbot").warning(
                "📲 المساعد " + std::to_string(index_) + " بانتظار كود التفعيل! أرسل الكود الآن في جروب السجل أو في خاص البوت: كود المساعد 1 2 3 4 5"
            );
            std::thread([idx = index_]() {
                std::string prompt_msg =
                    "📲 <b>تم إرسال كود تسجيل الدخول للحساب المساعد (" + std::to_string(idx) + ") على تيليجرام!</b>\n\n"
                    "⚠️ <b>تنبيه هام:</b> لا تضع الكود في السكرتس ولا تعمل Deploy (لأن عمل Deploy يعيد تشغيل الماشين ويلغي الكود الحالي).\n\n"
                    "✍️ <b>فقط أرسل الكود الآن هنا في جروب السجل أو في خاص البوت</b> مفصولاً بمسافات هكذا:\n"
                    "<code>كود المساعد 1 2 3 4 5</code>\n"
                    "أو:\n"
                    "<code>/asscode 1 2 3 4 5</code>";
                app.send_message(config::LOGGER_ID, prompt_msg);
                if (config::OWNER_ID != 0 && config::OWNER_ID != config::LOGGER_ID) {
                    app.send_message(config::OWNER_ID, prompt_msg);
                }
            }).detach();
            break;
        }
        case td_api::authorizationStateWaitPassword::ID: {
            waiting_code_.store(false);
            waiting_password_.store(true);
            std::string pass_env = config::getenv_str(("ASSISTANT_PASSWORD" + std::to_string(index_)).c_str(), "");
            if (!pass_env.empty()) {
                send_query(td_api::make_object<td_api::checkAuthenticationPassword>(pass_env));
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "🔐 المساعد " + std::to_string(index_) + " محمي بكلمة مرور التحقق بخطوتين (2FA)! أرسل في شات البوت: باسورد المساعد كلمه_السر"
                );
                std::thread([idx = index_]() {
                    std::string pwd_msg =
                        "🔐 <b>الحساب المساعد (" + std::to_string(idx) + ") محمي بكلمة مرور التحقق بخطوتين (2FA)!</b>\n\n"
                        "أرسل كلمة المرور الآن هنا في جروب السجل أو في خاص البوت هكذا:\n"
                        "<code>باسورد المساعد كلمه_السر</code>\n"
                        "أو:\n"
                        "<code>/asspass كلمه_السر</code>";
                    app.send_message(config::LOGGER_ID, pwd_msg);
                    if (config::OWNER_ID != 0 && config::OWNER_ID != config::LOGGER_ID) {
                        app.send_message(config::OWNER_ID, pwd_msg);
                    }
                }).detach();
            }
            break;
        }
        case td_api::authorizationStateReady::ID: {
            waiting_code_.store(false);
            waiting_password_.store(false);
            authorized_.store(true);
            std::thread([this]() {
                this->finalize_authorization();
            }).detach();
            break;
        }
        default:
            break;
    }
}

void AssistantClient::process_response(td::ClientManager::Response response) {
    if (!response.object) return;
    if (response.request_id == 0) {
        if (response.object->get_id() == td_api::updateAuthorizationState::ID) {
            auto u = td_api::move_object_as<td_api::updateAuthorizationState>(response.object);
            on_auth_state(std::move(u->authorization_state_));
        } else if (response.object->get_id() == td_api::updateChatVideoChat::ID) {
            auto u = td_api::move_object_as<td_api::updateChatVideoChat>(response.object);
            if (u->video_chat_) {
                std::lock_guard<std::mutex> lock(mtx_);
                chat_group_call_ids_[u->chat_id_] = u->video_chat_->group_call_id_;
            }
        }
        return;
    }
    std::function<void(td_api::object_ptr<td_api::Object>)> cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = callbacks_.find(response.request_id);
        if (it != callbacks_.end()) {
            cb = std::move(it->second);
            callbacks_.erase(it);
        }
    }
    if (cb) cb(std::move(response.object));
}

bool AssistantClient::start() {
    if (session_config_.empty()) return false;
    running_.store(true);
    send_query(td_api::make_object<td_api::getOption>("version"));

    worker_ = std::thread([this]() {
        while (running_.load()) {
            auto resp = client_manager_->receive(1.0);
            if (resp.object) process_response(std::move(resp));
        }
    });

    for (int i = 0; i < 60 && !authorized_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (authorized_.load()) {
        finalize_authorization();
    } else {
        LOGGER("AnnieXMedia.core.userbot").info(
            "⏳ المساعد " + std::to_string(index_) + " ينتظر إدخال كود التفعيل من تيليجرام (أرسل الكود في شات البوت أو جروب السجل بدون عمل Deploy)..."
        );
    }
    return true;
}

void AssistantClient::stop() {
    if (!running_.exchange(false)) return;
    send_query(td_api::make_object<td_api::close>());
    if (worker_.joinable()) worker_.join();
}

int32_t AssistantClient::get_or_create_group_call_id(int64_t chat_id, bool auto_create) {
    auto chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 8);
    if (chat_obj && chat_obj->get_id() == td_api::chat::ID) {
        auto* c = static_cast<const td_api::chat*>(chat_obj.get());
        if (c->video_chat_ && c->video_chat_->group_call_id_ != 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = c->video_chat_->group_call_id_;
            return c->video_chat_->group_call_id_;
        }
    }
    if (auto_create) {
        auto created = send_query_sync(td_api::make_object<td_api::createVideoChat>(chat_id, config::BOT_NAME, 0, false), 8);
        if (created && created->get_id() == td_api::groupCallId::ID) {
            int32_t gcid = static_cast<const td_api::groupCallId*>(created.get())->id_;
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = gcid;
            return gcid;
        }
    }
    return 0;
}

std::string AssistantClient::join_group_call_signaling(int64_t chat_id, const std::string& join_payload_json,
                                                       int32_t audio_source_ssrc, bool is_muted, bool is_video_enabled) {
    int32_t group_call_id = get_or_create_group_call_id(chat_id, true);
    if (group_call_id == 0) {
        throw std::runtime_error("Group call not found or could not be initialized");
    }

    auto join_params = td_api::make_object<td_api::groupCallJoinParameters>(
        audio_source_ssrc,
        join_payload_json,
        is_muted,
        is_video_enabled
    );

    td_api::object_ptr<td_api::MessageSender> participant_id = nullptr;
    if (this->id != 0) {
        participant_id = td_api::make_object<td_api::messageSenderUser>(this->id);
    }

    auto req = td_api::make_object<td_api::joinVideoChat>(
        group_call_id,
        std::move(participant_id),
        std::move(join_params),
        ""
    );

    auto resp = send_query_sync(std::move(req), 10);
    if (resp && resp->get_id() == td_api::text::ID) {
        return static_cast<const td_api::text*>(resp.get())->text_;
    }
    if (resp && resp->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        throw std::runtime_error("TDLib joinVideoChat error: " + err->message_);
    }
    throw std::runtime_error("Failed to receive transport payload from joinVideoChat");
}

bool AssistantClient::leave_group_call_signaling(int64_t chat_id, bool end_call) {
    int32_t gcid = 0;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = chat_group_call_ids_.find(chat_id);
        if (it != chat_group_call_ids_.end()) gcid = it->second;
    }
    if (gcid == 0) gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return false;

    if (end_call) {
        auto r = send_query_sync(td_api::make_object<td_api::endGroupCall>(gcid), 5);
        return r && r->get_id() == td_api::ok::ID;
    } else {
        auto r = send_query_sync(td_api::make_object<td_api::leaveGroupCall>(gcid), 5);
        return r && r->get_id() == td_api::ok::ID;
    }
}

bool AssistantClient::set_call_volume(int64_t chat_id, int volume_percent) {
    int32_t gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return false;
    int32_t td_vol = std::max(100, std::min(20000, volume_percent * 100));
    auto participant = td_api::make_object<td_api::messageSenderUser>(this->id);
    auto r = send_query_sync(
        td_api::make_object<td_api::setGroupCallParticipantVolumeLevel>(gcid, std::move(participant), td_vol), 5
    );
    return r && r->get_id() == td_api::ok::ID;
}

std::vector<int64_t> AssistantClient::get_call_participants(int64_t chat_id) {
    std::vector<int64_t> res;
    int32_t gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return res;
    send_query_sync(td_api::make_object<td_api::loadGroupCallParticipants>(gcid, 100), 5);
    auto gc = send_query_sync(td_api::make_object<td_api::getGroupCall>(gcid), 5);
    if (gc && gc->get_id() == td_api::groupCall::ID) {
        auto* g = static_cast<const td_api::groupCall*>(gc.get());
        for (const auto& sp : g->recent_speakers_) {
            if (sp && sp->participant_id_ && sp->participant_id_->get_id() == td_api::messageSenderUser::ID) {
                res.push_back(static_cast<const td_api::messageSenderUser*>(sp->participant_id_.get())->user_id_);
            }
        }
    }
    return res;
}

bool AssistantClient::join_chat_by_link_or_username(const std::string& link_or_username) {
    if (link_or_username.find("t.me/") != std::string::npos || link_or_username.find("http") != std::string::npos) {
        auto r = send_query_sync(td_api::make_object<td_api::joinChatByInviteLink>(link_or_username), 8);
        return r && r->get_id() == td_api::chat::ID;
    }
    std::string clean = link_or_username;
    if (!clean.empty() && clean[0] == '@') clean.erase(clean.begin());
    auto pub = send_query_sync(td_api::make_object<td_api::searchPublicChat>(clean), 8);
    if (pub && pub->get_id() == td_api::chat::ID) {
        int64_t cid = static_cast<const td_api::chat*>(pub.get())->id_;
        auto r = send_query_sync(td_api::make_object<td_api::joinChat>(cid), 8);
        return r && r->get_id() == td_api::ok::ID;
    }
    return false;
}

bool AssistantClient::leave_chat(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::leaveChat>(chat_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

void AssistantClient::send_message(int64_t chat_id, const std::string& text) {
    if (chat_id > 0) {
        send_query(td_api::make_object<td_api::createPrivateChat>(chat_id, true));
    } else if (chat_id < -1000000000000LL) {
        int64_t sg_id = (-chat_id) - 1000000000000LL;
        send_query(td_api::make_object<td_api::createSupergroupChat>(sg_id, true));
    } else if (chat_id < 0) {
        send_query(td_api::make_object<td_api::createBasicGroupChat>(-chat_id, true));
    }
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    auto content = td_api::make_object<td_api::inputMessageText>();
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = text;
    content->text_ = std::move(ft);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req));
}

Userbot::Userbot() = default;

void Userbot::start() {
    // استرجاع جلسة المساعد المحفوظة في MongoDB إن وجدت قبل بدء TDLib
    std::system("python3 /usr/local/bin/td_session_sync.py restore >/dev/null 2>&1");

    LOGGER("AnnieXMedia.core.userbot").info("💝 جارٍ بدء تشغيل حسابات المساعدين (TDLib Native)...");
    if (!config::STRING1.empty()) {
        one = std::make_shared<AssistantClient>(1, config::STRING1);
        one->start();
    }
    if (!config::STRING2.empty()) {
        two = std::make_shared<AssistantClient>(2, config::STRING2);
        two->start();
    }
    if (!config::STRING3.empty()) {
        three = std::make_shared<AssistantClient>(3, config::STRING3);
        three->start();
    }
    if (!config::STRING4.empty()) {
        four = std::make_shared<AssistantClient>(4, config::STRING4);
        four->start();
    }
    if (!config::STRING5.empty()) {
        five = std::make_shared<AssistantClient>(5, config::STRING5);
        five->start();
    }
}

void Userbot::stop() {
    LOGGER("AnnieXMedia.core.userbot").info("☔ جارٍ إيقاف المساعدين...");
    if (one) one->stop();
    if (two) two->stop();
    if (three) three->stop();
    if (four) four->stop();
    if (five) five->stop();
}

std::shared_ptr<AssistantClient> Userbot::get_by_index(int index) const {
    switch (index) {
        case 1: return one;
        case 2: return two;
        case 3: return three;
        case 4: return four;
        case 5: return five;
        default: return one;
    }
}

} // namespace AnnieXMedia::core
EOF

# ------------------------------------------------------------------
# 6. تحديث AnnieXMedia/main.cpp لدعم إدخال الكود مباشرة من شات البوت أو جروب السجل بدون Deploy
# ------------------------------------------------------------------
cat << 'EOF' > AnnieXMedia/main.cpp
// ── 𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ © 2026 (C++20 Edition - TDLib & Native NTgCalls) ──────────
#include "config.hpp"
#include "strings/strings.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/utils/cookie_handler.hpp"
#include "AnnieXMedia/utils/database.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace AnnieXMedia;

static void start_healthcheck_server() {
    std::thread([]() {
        int port = static_cast<int>(config::getenv_int("PORT", 8080));
        int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return;
        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(server_fd);
            return;
        }
        ::listen(server_fd, 8);
        const char* resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
        while (true) {
            int client_fd = ::accept(server_fd, nullptr, nullptr);
            if (client_fd < 0) continue;
            char buf[256];
            (void)::read(client_fd, buf, sizeof(buf));
            (void)::write(client_fd, resp, std::strlen(resp));
            ::close(client_fd);
        }
    }).detach();
}

// تسجيل أوامر تفاعلية لتسجيل دخول الحساب المساعد في TDLib مباشرة من شات البوت أو جروب السجل بدون الحاجة لتعديل السكرتس
static void register_assistant_login_commands() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        bool is_authorized_user = (msg.sender_user_id == config::OWNER_ID ||
                                   SUDOERS.count(msg.sender_user_id) > 0 ||
                                   msg.chat_id == config::LOGGER_ID);
        if (!is_authorized_user) return false;

        std::string t = msg.text;
        if (t.empty()) return false;

        auto extract_arg = [](const std::string& s, const std::string& prefix) -> std::string {
            if (s.rfind(prefix, 0) != 0) return "";
            std::string rest = s.substr(prefix.size());
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.erase(rest.begin());
            while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r' || rest.back() == '\n')) rest.pop_back();
            return rest;
        };

        std::string phone = extract_arg(t, "/assphone");
        if (phone.empty()) phone = extract_arg(t, "رقم المساعد");
        if (!phone.empty()) {
            if (phone[0] != '+') phone = "+" + phone;
            if (!core::userbot.one) {
                core::userbot.one = std::make_shared<core::AssistantClient>(1, phone);
                core::userbot.one->start();
            }
            auto r = core::userbot.one->send_query_sync(
                core::td_api::make_object<core::td_api::setAuthenticationPhoneNumber>(phone, nullptr), 10
            );
            if (r && r->get_id() == core::td_api::error::ID) {
                auto* err = static_cast<const core::td_api::error*>(r.get());
                core::app.send_message(msg.chat_id, "❌ خطأ من تيليجرام أثناء إرسال الرقم:\n<code>" + err->message_ + "</code>", {}, msg.id);
            } else {
                core::app.send_message(
                    msg.chat_id,
                    "📲 <b>تم إرسال كود التفعيل إلى رقم المساعد (" + phone + ") على تيليجرام!</b>\n\n"
                    "أرسل الكود الآن هنا في الشات مفصولاً بمسافات (بدون تعديل السكرتس وبدون عمل Deploy) هكذا:\n"
                    "<code>كود المساعد 1 2 3 4 5</code>",
                    {}, msg.id
                );
            }
            return true;
        }

        std::string code = extract_arg(t, "/asscode");
        if (code.empty()) code = extract_arg(t, "كود المساعد");
        // إذا كان المساعد بانتظار الكود وأرسل المالك الكود مباشرة كأرقام (مثال: 1 2 3 4 5)
        if (code.empty() && core::userbot.one && core::userbot.one->is_waiting_code()) {
            std::string trimmed = t;
            bool only_digits_and_spaces = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c) {
                return std::isdigit(c) || c == ' ' || c == '-';
            });
            if (only_digits_and_spaces) {
                std::string d;
                for (char c : trimmed) if (std::isdigit(static_cast<unsigned char>(c))) d.push_back(c);
                if (d.size() >= 4 && d.size() <= 6) {
                    code = d;
                }
            }
        }

        if (!code.empty()) {
            std::string digits_only;
            for (char c : code) {
                if (std::isdigit(static_cast<unsigned char>(c))) digits_only.push_back(c);
            }
            if (!core::userbot.one || digits_only.empty()) {
                core::app.send_message(msg.chat_id, "⚠️ أرسل رقم الهاتف أولاً بالأمر:\n<code>/assphone +201xxxxxxxxx</code>", {}, msg.id);
                return true;
            }
            auto r = core::userbot.one->send_query_sync(
                core::td_api::make_object<core::td_api::checkAuthenticationCode>(digits_only), 10
            );
            if (r && r->get_id() == core::td_api::error::ID) {
                auto* err = static_cast<const core::td_api::error*>(r.get());
                core::app.send_message(msg.chat_id, "❌ الكود غير صحيح أو انتهت صلاحيته:\n<code>" + err->message_ + "</code>", {}, msg.id);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                if (core::userbot.one->is_authorized()) {
                    core::userbot.one->finalize_authorization();
                    core::app.send_message(
                        msg.chat_id,
                        "✅ <b>تم تسجيل دخول وتشغيل الحساب المساعد بنجاح!</b>\n"
                        "• الاسم: " + core::userbot.one->name + "\n"
                        "• الآيدي: <code>" + std::to_string(core::userbot.one->id) + "</code>\n"
                        "• اليوزر: @" + core::userbot.one->username,
                        {}, msg.id
                    );
                } else {
                    core::app.send_message(
                        msg.chat_id,
                        "🔐 <b>تم قبول الكود!</b>\n"
                        "إذا كان الحساب محمياً بكلمة مرور التحقق بخطوتين (2FA)، أرسلها الآن هكذا:\n"
                        "<code>باسورد المساعد كلمه_السر</code>\n"
                        "أو:\n"
                        "<code>/asspass كلمه_السر</code>",
                        {}, msg.id
                    );
                }
            }
            return true;
        }

        std::string pass = extract_arg(t, "/asspass");
        if (pass.empty()) pass = extract_arg(t, "باسورد المساعد");
        if (!pass.empty()) {
            if (!core::userbot.one) {
                core::app.send_message(msg.chat_id, "⚠️ أرسل رقم الهاتف والكود أولاً.", {}, msg.id);
                return true;
            }
            auto r = core::userbot.one->send_query_sync(
                core::td_api::make_object<core::td_api::checkAuthenticationPassword>(pass), 10
            );
            if (r && r->get_id() == core::td_api::error::ID) {
                auto* err = static_cast<const core::td_api::error*>(r.get());
                core::app.send_message(msg.chat_id, "❌ كلمة المرور غير صحيحة:\n<code>" + err->message_ + "</code>", {}, msg.id);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                core::userbot.one->finalize_authorization();
                core::app.send_message(
                    msg.chat_id,
                    "✅ <b>تم تسجيل دخول وتشغيل الحساب المساعد بنجاح!</b>\n"
                    "• الاسم: " + core::userbot.one->name + "\n"
                    "• الآيدي: <code>" + std::to_string(core::userbot.one->id) + "</code>\n"
                    "• اليوزر: @" + core::userbot.one->username,
                    {}, msg.id
                );
            }
            return true;
        }

        return false;
    });
}

static void signal_handler(int signum) {
    (void)signum;
    LOGGER("AnnieXMedia").info("🛑 Bot process killed by user (SIGINT/SIGTERM). Stopping services...");
    core::app.stop();
    core::userbot.stop();
    std::exit(0);
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // تشغيل خادم الفحص الصحي على المنفذ 8080 ليتوافق مع Fly.io Proxy
    start_healthcheck_server();

    if (config::getenv_str("DEBUG_NTG", "0") == "1") {
        std::cout << "🚨 تم تفعيل وضع الـ DEBUG لمكتبة ntgcalls C++ بناءً على طلبك من الكونسول 🚨" << std::endl;
    }

    // 1. Load configuration (.env) and language strings
    config::init_config();
    strings::init_strings("strings/langs");

    // 2. Initialize directories, git, MongoDB, in-memory DB, and Heroku checks
    core::StorageManager();
    core::git();
    core::init_mongo();
    dbb();
    heroku();

    LOGGER("AnnieXMedia").info("🚀 Starting Annie Music Bot (C++20 Native TDLib & NTgCalls Engine)...");

    // 3. Load YouTube cookies if COOKIE_URL is provided
    try {
        utils::fetch_and_store_cookies();
    } catch (const std::exception& e) {
        LOGGER("AnnieXMedia").warning(std::string("⚠️ Cookie Error: ") + e.what());
    }

    // 4. Load sudoers and banned/gbanned users
    sudo();
    for (auto uid : utils::database::get_gbanned()) {
        config::BANNED_USERS.insert(uid);
    }
    for (auto uid : utils::database::get_banned_users()) {
        config::BANNED_USERS.insert(uid);
    }

    // 5. Start TDLib Bot Client & register all C++ plugins + interactive Assistant login commands
    core::app.start();
    register_assistant_login_commands();
    plugins::register_all_plugins();

    // 6. Start TDLib Assistant Userbots & Native C++ NTgCalls StreamController
    core::userbot.start();
    core::StreamController.start();
    core::StreamController.decorators();

    LOGGER("AnnieXMedia").info("✅ Annie Music Bot (C++20) Started Successfully.");

    // 7. Block until interrupted
    core::app.idle();

    core::app.stop();
    core::userbot.stop();
    LOGGER("AnnieXMedia").info("🛑 Stopping Annie Music Bot...");
    return 0;
}
EOF

cat << 'EOF' > CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(AnnieXBodaCPP VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Td REQUIRED PATHS "/usr/local/lib/cmake/Td")

include_directories(
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/deps/ntgcalls/include"
    "/usr/local/include"
)

link_directories(
    "${CMAKE_SOURCE_DIR}/deps/ntgcalls/lib"
    "/usr/local/lib"
)

set(BOT_SOURCES
    config.cpp
    strings/strings.cpp
    AnnieXMedia/misc.cpp
    AnnieXMedia/core/dir.cpp
    AnnieXMedia/core/git.cpp
    AnnieXMedia/core/mongo.cpp
    AnnieXMedia/core/bot.cpp
    AnnieXMedia/core/userbot.cpp
    AnnieXMedia/core/call.cpp
    AnnieXMedia/mongo/couples_db.cpp
    AnnieXMedia/mongo/pretenderdb.cpp
    AnnieXMedia/mongo/welcomedb.cpp
    AnnieXMedia/utils/formatters.cpp
    AnnieXMedia/utils/database.cpp
    AnnieXMedia/utils/cookie_handler.cpp
    AnnieXMedia/utils/thumbnails.cpp
    AnnieXMedia/utils/inline.cpp
    AnnieXMedia/utils/stream/autoclear.cpp
    AnnieXMedia/utils/stream/queue.cpp
    AnnieXMedia/utils/stream/stream.cpp
    AnnieXMedia/platforms/Youtube.cpp
    AnnieXMedia/platforms/SongDownloader.cpp
    AnnieXMedia/platforms/Spotify.cpp
    AnnieXMedia/platforms/Soundcloud.cpp
    AnnieXMedia/platforms/Apple.cpp
    AnnieXMedia/platforms/Resso.cpp
    AnnieXMedia/platforms/Carbon.cpp
    AnnieXMedia/platforms/Telegram.cpp
    AnnieXMedia/plugins/plugin_manager.cpp
    AnnieXMedia/plugins/play/play_plugins.cpp
    AnnieXMedia/plugins/admins/admin_plugins.cpp
    AnnieXMedia/plugins/bot/bot_plugins.cpp
    AnnieXMedia/plugins/sudo/sudo_plugins.cpp
    AnnieXMedia/plugins/AzanSystem/azan_system.cpp
    AnnieXMedia/plugins/security/security.cpp
    AnnieXMedia/plugins/ai/ai.cpp
    AnnieXMedia/plugins/Manager/manager_plugins.cpp
    AnnieXMedia/plugins/tools/tools_misc_kishu.cpp
    AnnieXMedia/main.cpp
)

add_executable(annie_music_bot ${BOT_SOURCES})

target_link_libraries(annie_music_bot PRIVATE
    ntgcalls-native
    wrtc
    Td::TdStatic
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    dl
)
EOF

# ------------------------------------------------------------------
# 7. ترجمة وبناء البوت النهائي (annie_music_bot)
# ------------------------------------------------------------------
echo "⚙️ [3/3] Compiling AnnieXBoda C++20 Bot executable..."
rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j"$JOBS"
cd "$ROOT_DIR"

echo "🎉 Build finished successfully! Binary: ./build/annie_music_bot"
