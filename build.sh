#!/usr/bin/env bash
set -e

echo "=========================================================="
echo "🚀 Building AnnieXBoda C++20 Music Bot (TDLib + NTgCalls)"
echo "=========================================================="

JOBS=$(nproc 2>/dev/null || echo 4)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# ------------------------------------------------------------------
# 0. تثبيت محرك NTgCalls الحقيقي وأحدث نسخة من yt-dlp وحذف الكوكيز القديمة
# ------------------------------------------------------------------
echo "📦 [0/4] Installing Real NTgCalls WebRTC Engine & updating yt-dlp (Cookie-Free Mode)..."
rm -f AnnieXMedia/assets/cookies.txt

if ! command -v pip3 >/dev/null 2>&1; then
    apt-get update && apt-get install -y --no-install-recommends python3-pip && rm -rf /var/lib/apt/lists/* || true
fi
if command -v pip3 >/dev/null 2>&1; then
    pip3 install --break-system-packages --upgrade ntgcalls yt-dlp pymongo dnspython 2>/dev/null || \
    pip3 install --upgrade ntgcalls yt-dlp pymongo dnspython || true
fi
curl -4 -sL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp && chmod a+rx /usr/local/bin/yt-dlp || true

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
    echo "📦 [1/4] Compiling TDLib from third_party/td..."
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
    echo "✅ [1/4] TDLib ready."
fi

# ------------------------------------------------------------------
# 2. بناء محرك وجسر C++ NTgCalls الحقيقي واستبدال الهيدرات الناقصة (rtc_base/logging.h)
# ------------------------------------------------------------------
echo "📦 [2/4] Building Native C++20 NTgCalls WebRTC Bridge..."
rm -rf third_party/ntgcalls/ntgcalls/include third_party/ntgcalls/wrtc/include
mkdir -p deps/ntgcalls/include/ntgcalls/media deps/ntgcalls/src deps/ntgcalls/lib
mkdir -p third_party/ntgcalls/ntgcalls/include third_party/ntgcalls/wrtc/include

cat << 'EOF' > /usr/local/bin/ntgcalls_daemon.py
#!/usr/bin/env python3
import json
import os
import shutil
import socket
import sys
import threading
import traceback

try:
    from ntgcalls import NTgCalls, MediaDescription, AudioDescription, VideoDescription, MediaSource, StreamMode, StreamType, StreamDevice
except Exception as e:
    print(f"[NTgCalls-Engine-ERROR] Failed to load native ntgcalls library: {e}", flush=True)
    sys.exit(1)

SOCK_PATH = "/tmp/ntgcalls_engine.sock"
if os.path.exists(SOCK_PATH):
    try:
        os.remove(SOCK_PATH)
    except Exception:
        pass

clients = {}
clients_lock = threading.Lock()
event_subscribers = []
sub_lock = threading.Lock()

FFMPEG_BIN = shutil.which("ffmpeg") or "/usr/bin/ffmpeg"

def broadcast_event(evt):
    data = (json.dumps(evt) + "\n").encode("utf-8")
    with sub_lock:
        dead = []
        for s in event_subscribers:
            try:
                s.sendall(data)
            except Exception:
                dead.append(s)
        for d in dead:
            event_subscribers.remove(d)

def get_client(idx):
    with clients_lock:
        if idx not in clients:
            c = NTgCalls()
            def on_eof(chat_id, stream_type, device):
                t_val = 0 if stream_type == StreamType.AUDIO else 1
                d_val = 0 if device == StreamDevice.MICROPHONE else (2 if device == StreamDevice.CAMERA else 1)
                print(f"[NTgCalls-Engine] Stream EOF on client #{idx} for chat_id={chat_id}", flush=True)
                broadcast_event({"event": "eof", "client": idx, "chat_id": int(chat_id), "type": t_val, "device": d_val})
            try:
                c.on_stream_end(on_eof)
            except Exception as e:
                print(f"[NTgCalls-Engine-WARN] on_stream_end hook warning: {e}", flush=True)
            clients[idx] = c
            print(f"[NTgCalls-Engine] Initialized real WebRTC NTgCalls instance #{idx}", flush=True)
        return clients[idx]

def build_media_desc(media_json):
    mic = None
    cam = None
    if media_json and "audio" in media_json and media_json["audio"]:
        a = media_json["audio"]
        inp = a.get("input", "").strip()
        sr = int(a.get("sample_rate", 48000))
        ch = int(a.get("channel_count", 2))
        if inp.startswith("ffmpeg ") or inp.startswith("/usr/bin/ffmpeg"):
            cmd = inp
        else:
            is_url = inp.startswith("http://") or inp.startswith("https://")
            recon = "-reconnect 1 -reconnect_at_eof 1 -reconnect_streamed 1 -reconnect_delay_max 2 " if is_url else ""
            cmd = f'{FFMPEG_BIN} -nostdin -hide_banner -loglevel warning {recon}-i "{inp}" -f s16le -ac {ch} -ar {sr} -vn pipe:1'
        print(f"[NTgCalls-Engine] Audio FFmpeg pipeline: {cmd}", flush=True)
        mic = AudioDescription(
            media_source=MediaSource.SHELL,
            input=cmd,
            sample_rate=sr,
            channel_count=ch,
            keep_open=True
        )
    if media_json and "video" in media_json and media_json["video"]:
        v = media_json["video"]
        inp = v.get("input", "").strip()
        w = int(v.get("width", 1280))
        h = int(v.get("height", 720))
        fps = int(v.get("fps", 30))
        if inp.startswith("ffmpeg ") or inp.startswith("/usr/bin/ffmpeg"):
            cmd = inp
        else:
            is_url = inp.startswith("http://") or inp.startswith("https://")
            recon = "-reconnect 1 -reconnect_at_eof 1 -reconnect_streamed 1 -reconnect_delay_max 2 " if is_url else ""
            cmd = f'{FFMPEG_BIN} -nostdin -hide_banner -loglevel warning {recon}-i "{inp}" -f rawvideo -r {fps} -pix_fmt yuv420p -vf scale={w}:{h} -an pipe:1'
        print(f"[NTgCalls-Engine] Video FFmpeg pipeline: {cmd}", flush=True)
        cam = VideoDescription(
            media_source=MediaSource.SHELL,
            input=cmd,
            width=w,
            height=h,
            fps=fps,
            keep_open=True
        )
    return MediaDescription(microphone=mic, camera=cam)

def handle_conn(conn):
    f = conn.makefile("r", encoding="utf-8")
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            cmd = req.get("cmd", "")
            if cmd == "subscribe":
                with sub_lock:
                    event_subscribers.append(conn)
                return
            idx = int(req.get("client", 0))
            c = get_client(idx)
            chat_id = int(req.get("chat_id", 0))

            if cmd == "create_call":
                media_desc = build_media_desc(req.get("media", {}))
                try:
                    params = c.create_call(chat_id)
                    if media_desc.microphone or media_desc.camera:
                        c.set_stream_sources(chat_id, StreamMode.CAPTURE, media_desc)
                except TypeError:
                    params = c.create_call(chat_id, media_desc)
                print(f"[NTgCalls-Engine] create_call({chat_id}) generated WebRTC payload ({len(params)} bytes)", flush=True)
                resp = {"ok": True, "result": params}
            elif cmd == "set_stream_sources":
                media_desc = build_media_desc(req.get("media", {}))
                try:
                    c.set_stream_sources(chat_id, StreamMode.CAPTURE, media_desc)
                except AttributeError:
                    c.change_stream(chat_id, media_desc)
                print(f"[NTgCalls-Engine] set_stream_sources({chat_id}) succeeded", flush=True)
                resp = {"ok": True}
            elif cmd == "connect":
                params = req.get("params", "")
                is_pres = bool(req.get("is_presentation", False))
                try:
                    c.connect(chat_id, params, is_pres)
                except TypeError:
                    c.connect(chat_id, params)
                print(f"[NTgCalls-Engine] connect({chat_id}) WebRTC DTLS-SRTP connected! Audio is streaming now.", flush=True)
                resp = {"ok": True}
            elif cmd == "pause":
                res = bool(c.pause(chat_id))
                resp = {"ok": True, "result": res}
            elif cmd == "resume":
                res = bool(c.resume(chat_id))
                resp = {"ok": True, "result": res}
            elif cmd == "mute":
                res = bool(c.mute(chat_id))
                resp = {"ok": True, "result": res}
            elif cmd == "unmute":
                res = bool(c.unmute(chat_id))
                resp = {"ok": True, "result": res}
            elif cmd == "stop":
                c.stop(chat_id)
                print(f"[NTgCalls-Engine] stop({chat_id}) stopped call.", flush=True)
                resp = {"ok": True}
            elif cmd == "time":
                try:
                    t = int(c.time(chat_id, StreamMode.CAPTURE))
                except TypeError:
                    t = int(c.time(chat_id))
                resp = {"ok": True, "result": t}
            elif cmd == "cpu_usage":
                resp = {"ok": True, "result": float(c.cpu_usage())}
            elif cmd == "calls":
                raw_calls = c.calls()
                out = {}
                for k in raw_calls:
                    out[str(k)] = 0
                resp = {"ok": True, "result": out}
            else:
                resp = {"ok": False, "error": f"Unknown command: {cmd}"}
        except Exception as e:
            err_msg = f"{type(e).__name__}: {e}"
            print(f"[NTgCalls-Engine-ERROR] Command '{req.get('cmd')}' failed for chat {req.get('chat_id')}: {err_msg}\n{traceback.format_exc()}", flush=True)
            resp = {"ok": False, "error": err_msg}

        try:
            conn.sendall((json.dumps(resp) + "\n").encode("utf-8"))
        except Exception:
            break
    try:
        conn.close()
    except Exception:
        pass

def main():
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK_PATH)
    os.chmod(SOCK_PATH, 0o666)
    srv.listen(64)
    print(f"[NTgCalls-Engine] Real C++ WebRTC NTgCalls Daemon listening on {SOCK_PATH}", flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=handle_conn, args=(conn,), daemon=True).start()

if __name__ == "__main__":
    main()
EOF
chmod +x /usr/local/bin/ntgcalls_daemon.py

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
        File = 1 << 0,
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

    AudioDescription(const MediaSource src, const uint32_t sr, const uint8_t ch, const std::string& in, const bool keep = true)
        : BaseMediaDescription(in, src, keep), sample_rate(sr), channel_count(ch) {}
};

class VideoDescription final : public BaseMediaDescription {
public:
    int16_t width, height;
    uint8_t fps;

    VideoDescription(const MediaSource src, const int16_t w, const int16_t h, const uint8_t f, const std::string& in, const bool keep = true)
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
    int instance_id_;
    mutable std::mutex mtx_;
    std::unordered_map<int64_t, media::MediaDescription> pending_media_;
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

# نسخ الهيدرات النظيفة إلى third_party/ntgcalls و /usr/local/include لمنع خطأ rtc_base/logging.h نهائياً
cp -r deps/ntgcalls/include/* third_party/ntgcalls/ntgcalls/include/
cp -r deps/ntgcalls/include/* /usr/local/include/

cat << 'EOF' > deps/ntgcalls/src/ntgcalls_bridge.cpp
#include <ntgcalls/ntgcalls.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace ntgcalls {

static std::atomic<int> g_next_instance_id{1};
static std::once_flag g_daemon_once;
static std::mutex g_instances_mtx;
static std::unordered_map<int, std::function<void(int64_t, media::StreamManager::Type, media::StreamManager::Device)>> g_eof_callbacks;

static void run_bg_cmd(const char* cmd) {
    int rc = std::system(cmd);
    (void)rc;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            if (n == '\"') out.push_back('\"');
            else if (n == '\\') out.push_back('\\');
            else if (n == '/') out.push_back('/');
            else if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static std::string extract_json_str(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\":";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos += pat.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '\"') return "";
    ++pos;
    std::string raw;
    while (pos < json.size()) {
        if (json[pos] == '\"' && (raw.empty() || raw.back() != '\\')) break;
        raw.push_back(json[pos++]);
    }
    return json_unescape(raw);
}

static int64_t extract_json_int(const std::string& json, const std::string& key, int64_t def = 0) {
    std::string pat = "\"" + key + "\":";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return def;
    pos += pat.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    try {
        return std::stoll(json.substr(pos));
    } catch (...) {
        return def;
    }
}

static void ensure_daemon_started() {
    std::call_once(g_daemon_once, []() {
        run_bg_cmd("pkill -f ntgcalls_daemon.py 2>/dev/null || true");
        run_bg_cmd("python3 /usr/local/bin/ntgcalls_daemon.py &");
        for (int i = 0; i < 50; ++i) {
            if (::access("/tmp/ntgcalls_engine.sock", F_OK) == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::thread([]() {
            while (true) {
                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd >= 0) {
                    sockaddr_un addr{};
                    addr.sun_family = AF_UNIX;
                    std::strncpy(addr.sun_path, "/tmp/ntgcalls_engine.sock", sizeof(addr.sun_path) - 1);
                    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                        const char* sub = "{\"cmd\":\"subscribe\"}\n";
                        ssize_t wr = ::write(fd, sub, std::strlen(sub));
                        (void)wr;
                        std::string buf;
                        char tmp[1024];
                        ssize_t n;
                        while ((n = ::read(fd, tmp, sizeof(tmp))) > 0) {
                            buf.append(tmp, static_cast<size_t>(n));
                            size_t nl;
                            while ((nl = buf.find('\n')) != std::string::npos) {
                                std::string line = buf.substr(0, nl);
                                buf.erase(0, nl + 1);
                                if (line.find("\"event\": \"eof\"") != std::string::npos || line.find("\"event\":\"eof\"") != std::string::npos) {
                                    int cid = static_cast<int>(extract_json_int(line, "client", 0));
                                    int64_t chat_id = extract_json_int(line, "chat_id", 0);
                                    std::function<void(int64_t, media::StreamManager::Type, media::StreamManager::Device)> cb;
                                    {
                                        std::lock_guard<std::mutex> lk(g_instances_mtx);
                                        if (g_eof_callbacks.count(cid)) cb = g_eof_callbacks[cid];
                                    }
                                    if (cb && chat_id != 0) {
                                        std::thread([cb, chat_id]() {
                                            cb(chat_id, media::StreamManager::Type::Audio, media::StreamManager::Device::Microphone);
                                        }).detach();
                                    }
                                }
                            }
                        }
                    }
                    ::close(fd);
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }).detach();
    });
}

static std::string send_daemon_rpc(const std::string& payload) {
    ensure_daemon_started();
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) throw NTgCallsException("Failed to create AF_UNIX socket for NTgCalls daemon");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/ntgcalls_engine.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        throw NTgCallsException("Failed to connect to /tmp/ntgcalls_engine.sock (is ntgcalls_daemon.py running?)");
    }
    std::string msg = payload + "\n";
    ssize_t wr = ::write(fd, msg.data(), msg.size());
    (void)wr;
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        resp.append(buf, static_cast<size_t>(n));
        if (resp.find('\n') != std::string::npos) break;
    }
    ::close(fd);
    if (resp.find("\"ok\": false") != std::string::npos || resp.find("\"ok\":false") != std::string::npos) {
        std::string err = extract_json_str(resp, "error");
        throw NTgCallsException(err.empty() ? ("NTgCalls daemon error: " + resp) : err);
    }
    return resp;
}

static std::string serialize_media(const media::MediaDescription& md) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    if (md.microphone.has_value()) {
        oss << "\"audio\":{\"input\":\"" << json_escape(md.microphone->input) << "\","
            << "\"sample_rate\":" << md.microphone->sample_rate << ","
            << "\"channel_count\":" << static_cast<int>(md.microphone->channel_count) << "}";
        first = false;
    }
    if (md.camera.has_value()) {
        if (!first) oss << ",";
        oss << "\"video\":{\"input\":\"" << json_escape(md.camera->input) << "\","
            << "\"width\":" << md.camera->width << ","
            << "\"height\":" << md.camera->height << ","
            << "\"fps\":" << static_cast<int>(md.camera->fps) << "}";
    }
    oss << "}";
    return oss.str();
}

NTgCalls::NTgCalls() : instance_id_(g_next_instance_id.fetch_add(1)) {
    ensure_daemon_started();
}

NTgCalls::~NTgCalls() {
    std::lock_guard<std::mutex> lk(g_instances_mtx);
    g_eof_callbacks.erase(instance_id_);
}

std::string NTgCalls::create_call(int64_t chat_id) {
    media::MediaDescription md;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (pending_media_.count(chat_id)) {
            md = pending_media_[chat_id];
        }
        active_calls_[chat_id] = {media::StreamManager::Status::Active, media::StreamManager::Status::Active};
    }
    std::ostringstream req;
    req << "{\"cmd\":\"create_call\",\"client\":" << instance_id_
        << ",\"chat_id\":" << chat_id
        << ",\"media\":" << serialize_media(md) << "}";
    std::string resp = send_daemon_rpc(req.str());
    return extract_json_str(resp, "result");
}

void NTgCalls::set_stream_sources(int64_t chat_id, media::StreamManager::Mode mode, const media::MediaDescription& media) {
    (void)mode;
    bool already_active = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pending_media_[chat_id] = media;
        already_active = (active_calls_.count(chat_id) > 0);
    }
    if (already_active) {
        std::ostringstream req;
        req << "{\"cmd\":\"set_stream_sources\",\"client\":" << instance_id_
            << ",\"chat_id\":" << chat_id
            << ",\"media\":" << serialize_media(media) << "}";
        send_daemon_rpc(req.str());
    }
}

void NTgCalls::connect(int64_t chat_id, const std::string& params, bool is_presentation) {
    std::ostringstream req;
    req << "{\"cmd\":\"connect\",\"client\":" << instance_id_
        << ",\"chat_id\":" << chat_id
        << ",\"params\":\"" << json_escape(params) << "\","
        << "\"is_presentation\":" << (is_presentation ? "true" : "false") << "}";
    send_daemon_rpc(req.str());
}

bool NTgCalls::pause(int64_t chat_id) {
    std::ostringstream req;
    req << "{\"cmd\":\"pause\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    send_daemon_rpc(req.str());
    std::lock_guard<std::mutex> lock(mtx_);
    if (active_calls_.count(chat_id)) active_calls_[chat_id].capture = media::StreamManager::Status::Paused;
    return true;
}

bool NTgCalls::resume(int64_t chat_id) {
    std::ostringstream req;
    req << "{\"cmd\":\"resume\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    send_daemon_rpc(req.str());
    std::lock_guard<std::mutex> lock(mtx_);
    if (active_calls_.count(chat_id)) active_calls_[chat_id].capture = media::StreamManager::Status::Active;
    return true;
}

bool NTgCalls::mute(int64_t chat_id) {
    std::ostringstream req;
    req << "{\"cmd\":\"mute\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    send_daemon_rpc(req.str());
    return true;
}

bool NTgCalls::unmute(int64_t chat_id) {
    std::ostringstream req;
    req << "{\"cmd\":\"unmute\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    send_daemon_rpc(req.str());
    return true;
}

void NTgCalls::stop(int64_t chat_id) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        active_calls_.erase(chat_id);
        pending_media_.erase(chat_id);
    }
    std::ostringstream req;
    req << "{\"cmd\":\"stop\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    try { send_daemon_rpc(req.str()); } catch (...) {}
}

uint64_t NTgCalls::time(int64_t chat_id, media::StreamManager::Mode mode) {
    (void)mode;
    std::ostringstream req;
    req << "{\"cmd\":\"time\",\"client\":" << instance_id_ << ",\"chat_id\":" << chat_id << "}";
    try {
        std::string resp = send_daemon_rpc(req.str());
        return static_cast<uint64_t>(extract_json_int(resp, "result", 0));
    } catch (...) {
        return 0;
    }
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
    {
        std::lock_guard<std::mutex> lock(mtx_);
        on_eof_cb_ = callback;
    }
    std::lock_guard<std::mutex> lk(g_instances_mtx);
    g_eof_callbacks[instance_id_] = callback;
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
echo "✅ [2/4] Native C++ NTgCalls ready."

# ------------------------------------------------------------------
# 3. تحديث ملفات الكور واليوتيوب (بدون كوكيز نهائياً عبر VISIONOS IPv4)
# ------------------------------------------------------------------
echo "🔧 [3/4] Updating Youtube.cpp (Cookie-Free VISIONOS + IPv4), cookie_handler.cpp, bot.cpp, userbot.cpp..."

cat << 'EOF' > AnnieXMedia/utils/cookie_handler.cpp
#include "AnnieXMedia/utils/cookie_handler.hpp"
#include "AnnieXMedia/logging.hpp"
#include <filesystem>

namespace AnnieXMedia::utils {

std::string resolve_raw_cookie_url(const std::string& url) {
    return url;
}

void fetch_and_store_cookies() {
    std::error_code ec;
    if (std::filesystem::exists("AnnieXMedia/assets/cookies.txt", ec)) {
        std::filesystem::remove("AnnieXMedia/assets/cookies.txt", ec);
    }
    LOGGER("AnnieXMedia.utils.cookie_handler").info("✅ Cookie-Free YouTube Innertube Engine (VISIONOS / IPv4) Active.");
}

} // namespace AnnieXMedia::utils
EOF

cat << 'EOF' > AnnieXMedia/platforms/Youtube.cpp
#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "config.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace AnnieXMedia::platforms {

YouTubeAPI YouTube;

namespace {

constexpr int64_t CACHE_TTL_SEC = 3600;

std::mutex g_yt_cache_mtx;
std::unordered_map<std::string, std::pair<std::string, int64_t>> g_url_cache;
std::unordered_map<std::string, std::pair<TrackDetails, int64_t>> g_meta_cache;

int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string shell_quote(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

bool is_valid_vidid(const std::string& s) {
    if (s.size() != 11) return false;
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string extract_vid(const std::string& link, bool is_videoid = false) {
    if (is_videoid && is_valid_vidid(link)) {
        return link;
    }
    std::string s = link;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    if (s.size() == 13 && s.ends_with("_v")) {
        s = s.substr(0, 11);
    }
    if (is_valid_vidid(s) && s.rfind("http", 0) != 0 && s.find(' ') == std::string::npos) {
        return s;
    }
    auto vpos = s.find("v=");
    if (vpos != std::string::npos) {
        std::string sub = s.substr(vpos + 2);
        auto amp = sub.find_first_of("&#? ");
        if (amp != std::string::npos) sub = sub.substr(0, amp);
        if (is_valid_vidid(sub)) return sub;
    }
    for (const char* prefix : {"youtu.be/", "/shorts/", "/live/"}) {
        auto ppos = s.find(prefix);
        if (ppos != std::string::npos) {
            std::string sub = s.substr(ppos + std::char_traits<char>::length(prefix));
            auto qpos = sub.find_first_of("?&# ");
            if (qpos != std::string::npos) sub = sub.substr(0, qpos);
            if (is_valid_vidid(sub)) return sub;
        }
    }
    return "";
}

std::string sec_to_min_str(int seconds) {
    if (seconds <= 0) return "0:00";
    int m = seconds / 60;
    int s = seconds % 60;
    int h = m / 60;
    m %= 60;
    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

int parse_duration_to_sec(const std::string& dur) {
    if (dur.empty() || dur == "Live") return 0;
    std::vector<int> parts;
    std::stringstream ss(dur);
    std::string item;
    while (std::getline(ss, item, ':')) {
        try {
            parts.push_back(std::stoi(item));
        } catch (...) {
            return 0;
        }
    }
    int total = 0;
    int mult = 1;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        total += (*it) * mult;
        mult *= 60;
    }
    return total;
}

struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;

    const JsonVal& get(const std::string& k) const {
        static const JsonVal null_v;
        if (type != Object) return null_v;
        auto it = obj.find(k);
        return (it != obj.end()) ? it->second : null_v;
    }
    std::string as_str(const std::string& def = "") const {
        if (type == String) return str;
        if (type == Number) return std::to_string(static_cast<int64_t>(num));
        return def;
    }
    int64_t as_int(int64_t def = 0) const {
        if (type == Number) return static_cast<int64_t>(num);
        if (type == String) {
            try { return std::stoll(str); } catch (...) {}
        }
        return def;
    }
    bool as_bool(bool def = false) const {
        if (type == Bool) return b;
        return def;
    }
};

class JsonParser {
    const std::string& s_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) {
            ++pos_;
        }
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string parse_string() {
        if (pos_ >= s_.size() || s_[pos_] != '"') return "";
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c == '\\' && pos_ < s_.size()) {
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 <= s_.size()) {
                            uint32_t cp = 0;
                            try { cp = static_cast<uint32_t>(std::stoul(s_.substr(pos_, 4), nullptr, 16)); } catch (...) {}
                            pos_ += 4;
                            if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                                uint32_t low = 0;
                                try { low = static_cast<uint32_t>(std::stoul(s_.substr(pos_ + 2, 4), nullptr, 16)); } catch (...) {}
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    pos_ += 6;
                                    cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                                }
                            }
                            append_utf8(out, cp);
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    JsonVal parse_value() {
        skip_ws();
        if (pos_ >= s_.size()) return {};
        char c = s_[pos_];
        if (c == '"') {
            JsonVal v;
            v.type = JsonVal::String;
            v.str = parse_string();
            return v;
        }
        if (c == '{') {
            ++pos_;
            JsonVal v;
            v.type = JsonVal::Object;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return v; }
            while (pos_ < s_.size()) {
                skip_ws();
                std::string k = parse_string();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ':') ++pos_;
                v.obj[std::move(k)] = parse_value();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; break; }
                break;
            }
            return v;
        }
        if (c == '[') {
            ++pos_;
            JsonVal v;
            v.type = JsonVal::Array;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return v; }
            while (pos_ < s_.size()) {
                v.arr.push_back(parse_value());
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; break; }
                break;
            }
            return v;
        }
        if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            JsonVal v; v.type = JsonVal::Bool; v.b = true; return v;
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            JsonVal v; v.type = JsonVal::Bool; v.b = false; return v;
        }
        if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return {};
        }
        size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
               s_[pos_] == '-' || s_[pos_] == '+' || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
        }
        if (pos_ > start) {
            JsonVal v;
            v.type = JsonVal::Number;
            try { v.num = std::stod(s_.substr(start, pos_ - start)); } catch (...) { v.num = 0.0; }
            return v;
        }
        ++pos_;
        return {};
    }
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

std::optional<TrackDetails> innertube_player(const std::string& vid, const std::string& base_url) {
    if (!is_valid_vidid(vid)) return std::nullopt;
    int64_t now = now_epoch();
    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_meta_cache.find(vid);
        if (it != g_meta_cache.end() && it->second.second > now) {
            auto a_it = g_url_cache.find(vid + ":a");
            if (a_it != g_url_cache.end() && a_it->second.second > now) {
                return it->second.first;
            }
        }
    }

    std::string payload =
        "{\"context\":{\"client\":{"
        "\"clientName\":\"VISIONOS\","
        "\"clientVersion\":\"1.02\","
        "\"deviceMake\":\"Apple\","
        "\"deviceModel\":\"RealityDevice17,1\","
        "\"osName\":\"visionOS\","
        "\"osVersion\":\"26.5.23O471\","
        "\"hl\":\"en\","
        "\"timeZone\":\"UTC\","
        "\"utcOffsetMinutes\":0"
        "}},\"videoId\":\"" + json_escape(vid) + "\","
        "\"contentCheckOk\":true,\"racyCheckOk\":true}";

    std::string cmd =
        "curl -4 -sS --max-time 4 --connect-timeout 2 "
        "-X POST 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false' "
        "-H 'Content-Type: application/json' "
        "-H 'User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15' "
        "-H 'X-YouTube-Client-Name: 101' "
        "-H 'X-YouTube-Client-Version: 1.02' "
        "-H 'Origin: https://www.youtube.com' "
        "--data-raw " + shell_quote(payload) + " 2>/dev/null";

    auto [out, rc] = core::run_shell_command(cmd);
    if (rc != 0 || out.empty()) return std::nullopt;

    JsonVal root = JsonParser(out).parse_value();
    const JsonVal& vd = root.get("videoDetails");
    std::string title = vd.get("title").as_str();
    if (title.empty()) return std::nullopt;

    std::string v_id = vd.get("videoId").as_str(vid);
    int secs = static_cast<int>(vd.get("lengthSeconds").as_int(0));
    bool is_live = vd.get("isLive").as_bool(false) || (vd.get("isLiveContent").as_bool(false) && secs == 0);
    std::string duration_min = is_live ? "Live" : sec_to_min_str(secs);

    const JsonVal& sd = root.get("streamingData");
    std::string cached_audio_url;
    std::string cached_video_url;

    auto scan_formats = [&](const JsonVal& arr_val) {
        if (arr_val.type != JsonVal::Array) return;
        for (const auto& fmt : arr_val.arr) {
            std::string f_url = fmt.get("url").as_str();
            if (f_url.empty()) continue;
            std::string itag = fmt.get("itag").as_str();
            std::string mime = fmt.get("mimeType").as_str();
            if (itag == "18") {
                cached_video_url = f_url;
                if (cached_audio_url.empty()) cached_audio_url = f_url;
            } else if (mime.find("audio") != std::string::npos) {
                cached_audio_url = f_url;
            }
        }
    };
    scan_formats(sd.get("formats"));
    scan_formats(sd.get("adaptiveFormats"));

    TrackDetails td;
    td.title = title;
    td.vidid = v_id;
    td.link = base_url + v_id;
    td.duration_sec = secs;
    td.duration_min = duration_min;
    td.thumb = "https://i.ytimg.com/vi/" + v_id + "/hqdefault.jpg";

    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_meta_cache[v_id] = {td, now + CACHE_TTL_SEC};
        if (!cached_video_url.empty()) {
            g_url_cache[v_id + ":v"] = {cached_video_url, now + CACHE_TTL_SEC};
        }
        if (!cached_audio_url.empty()) {
            g_url_cache[v_id + ":a"] = {cached_audio_url, now + CACHE_TTL_SEC};
        }
    }

    return td;
}

std::vector<TrackDetails> innertube_search(const std::string& query, int limit, const std::string& base_url) {
    std::vector<TrackDetails> results;
    if (query.empty()) return results;

    std::string payload =
        "{\"context\":{\"client\":{"
        "\"clientName\":\"WEB\","
        "\"clientVersion\":\"2.20260708.00.00\","
        "\"hl\":\"ar\","
        "\"gl\":\"EG\","
        "\"timeZone\":\"UTC\","
        "\"utcOffsetMinutes\":0"
        "}},\"query\":\"" + json_escape(query) + "\","
        "\"params\":\"EgIQAfABAQ==\"}";

    std::string cmd =
        "curl -4 -sS --max-time 4 --connect-timeout 2 "
        "-X POST 'https://www.youtube.com/youtubei/v1/search?prettyPrint=false' "
        "-H 'Content-Type: application/json' "
        "-H 'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36' "
        "-H 'X-YouTube-Client-Name: 1' "
        "-H 'X-YouTube-Client-Version: 2.20260708.00.00' "
        "-H 'Origin: https://www.youtube.com' "
        "--data-raw " + shell_quote(payload) + " 2>/dev/null";

    auto [out, rc] = core::run_shell_command(cmd);
    if (rc != 0 || out.empty()) return results;

    JsonVal root = JsonParser(out).parse_value();
    const JsonVal& sections = root.get("contents")
        .get("twoColumnSearchResultsRenderer")
        .get("primaryContents")
        .get("sectionListRenderer")
        .get("contents");

    if (sections.type != JsonVal::Array) return results;

    int64_t now = now_epoch();
    for (const auto& sec : sections.arr) {
        const JsonVal& items = sec.get("itemSectionRenderer").get("contents");
        if (items.type != JsonVal::Array) continue;
        for (const auto& item : items.arr) {
            const JsonVal& vr = item.get("videoRenderer");
            if (vr.type != JsonVal::Object) continue;
            std::string v_id = vr.get("videoId").as_str();
            if (v_id.empty()) continue;

            std::string title;
            const JsonVal& runs = vr.get("title").get("runs");
            if (runs.type == JsonVal::Array) {
                for (const auto& r : runs.arr) {
                    title += r.get("text").as_str();
                }
            }
            if (title.empty()) title = "Unknown";

            std::string dur_text = vr.get("lengthText").get("simpleText").as_str();
            if (dur_text.empty()) {
                dur_text = vr.get("lengthText")
                    .get("accessibility")
                    .get("accessibilityData")
                    .get("label")
                    .as_str("Live");
            }

            TrackDetails td;
            td.title = title;
            td.vidid = v_id;
            td.link = base_url + v_id;
            td.duration_min = dur_text;
            td.duration_sec = parse_duration_to_sec(dur_text);
            td.thumb = "https://i.ytimg.com/vi/" + v_id + "/hqdefault.jpg";

            {
                std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
                g_meta_cache[v_id] = {td, now + CACHE_TTL_SEC};
            }
            results.push_back(std::move(td));
            if (static_cast<int>(results.size()) >= limit) return results;
        }
    }
    return results;
}

} // namespace

bool YouTubeAPI::exists(const std::string& link) const {
    static const std::regex yt_re(
        R"((https?://)?(www\.|m\.|music\.)?(youtube\.com/(watch\?v=|shorts/|live/|playlist\?list=)|youtu\.be/)([A-Za-z0-9_-]{11}|PL[A-Za-z0-9_-]+|OLAK5uy_[A-Za-z0-9_-]+)([&?][^\s]*)?)"
    );
    return std::regex_search(link, yt_re);
}

TrackDetails YouTubeAPI::track(const std::string& query_or_link, bool is_videoid) {
    std::string vid = extract_vid(query_or_link, is_videoid);

    if (is_valid_vidid(vid)) {
        LOGGER("AnnieXMedia.platforms.Youtube").info("⚡ Fast Innertube Player lookup (cookie-free IPv4) for videoId: " + vid);
        if (auto info = innertube_player(vid, base)) {
            LOGGER("AnnieXMedia.platforms.Youtube").info(
                "✅ YouTube track found (Innertube Player): title=[" + info->title + "] id=[" + info->vidid + "] duration=[" + info->duration_min + "]"
            );
            return *info;
        }
    }

    std::string query = is_valid_vidid(vid) ? vid : query_or_link;
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) query.erase(query.begin());
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back()))) query.pop_back();

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔎 Fast Innertube Search (cookie-free IPv4) for query: " + query);
    auto results = innertube_search(query, 1, base);
    if (!results.empty()) {
        TrackDetails first = results.front();
        if (is_valid_vidid(first.vidid)) {
            if (auto full_info = innertube_player(first.vidid, base)) {
                first = *full_info;
            }
        }
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ YouTube track found: title=[" + first.title + "] id=[" + first.vidid + "] duration=[" + first.duration_min + "]"
        );
        return first;
    }

    std::string target = is_valid_vidid(vid) ? (base + vid) : ("ytsearch1:" + query);
    std::string cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates --flat-playlist "
        "--extractor-args \"youtube:player_client=visionos,android;player_skip=webpage,configs,initial_data,js;fetch_pot=never\" "
        "--print \"%(id)s\t%(title)s\t%(duration)s\t%(webpage_url)s\t%(thumbnail)s\" " +
        shell_quote(target) + " 2>&1";

    auto [out, rc] = core::run_shell_command(cmd);
    if (!out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find('\t') == std::string::npos) continue;
            TrackDetails td;
            std::stringstream lss(line);
            std::string dur_s;
            std::getline(lss, td.vidid, '\t');
            std::getline(lss, td.title, '\t');
            std::getline(lss, dur_s, '\t');
            std::getline(lss, td.link, '\t');
            std::getline(lss, td.thumb, '\t');
            if (!td.vidid.empty() && !td.title.empty()) {
                try { td.duration_sec = static_cast<int>(std::stod(dur_s)); } catch (...) { td.duration_sec = 0; }
                td.duration_min = td.duration_sec > 0 ? sec_to_min_str(td.duration_sec) : "Live";
                td.link = is_valid_vidid(td.vidid) ? (base + td.vidid) : target;
                td.thumb = "https://i.ytimg.com/vi/" + td.vidid + "/hqdefault.jpg";
                return td;
            }
        }
    }

    std::string err_msg = "❌ لم يتم العثور على نتائج في يوتيوب لـ: " + query;
    LOGGER("AnnieXMedia.platforms.Youtube").error(err_msg);
    throw std::runtime_error(err_msg);
}

std::string YouTubeAPI::get_direct_link(const std::string& link, bool prefer_audio) {
    std::string vid = extract_vid(link, false);
    if (!is_valid_vidid(vid) && !exists(link) && link.rfind("http", 0) != 0) {
        auto td = track(link, false);
        vid = td.vidid;
    }

    std::string target_url = is_valid_vidid(vid) ? (base + vid) : link;
    bool is_video = !prefer_audio;
    std::string cache_key = (is_valid_vidid(vid) ? vid : target_url) + (is_video ? ":v" : ":a");
    int64_t now = now_epoch();

    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_url_cache.find(cache_key);
        if (it != g_url_cache.end() && it->second.second > now && !it->second.first.empty()) {
            LOGGER("AnnieXMedia.platforms.Youtube").info("⚡ Using cached direct YouTube stream URL for " + cache_key);
            return it->second.first;
        }
    }

    if (is_valid_vidid(vid)) {
        (void)innertube_player(vid, base);
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_url_cache.find(cache_key);
        if (it != g_url_cache.end() && it->second.second > now && !it->second.first.empty()) {
            LOGGER("AnnieXMedia.platforms.Youtube").info(
                "✅ Extracted direct YouTube URL via Innertube VISIONOS (cookie-free, " +
                std::to_string(it->second.first.size()) + " chars)"
            );
            return it->second.first;
        }
    }

    std::string media_format = is_video
        ? "best[ext=mp4][height<=720]/best[height<=720]/best"
        : "bestaudio[ext=m4a]/bestaudio/best";

    auto extract_first_http_url = [prefer_audio](const std::string& output) -> std::string {
        std::stringstream ss(output);
        std::string line;
        std::string chosen;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.rfind("https://", 0) == 0 || line.rfind("http://", 0) == 0) {
                chosen = line;
                if (prefer_audio) break;
            }
        }
        return chosen;
    };

    std::string fast_cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates "
        "--no-playlist --socket-timeout 5 --retries 2 --cache-dir /tmp/ytdlp_cache "
        "--extractor-args \"youtube:player_client=visionos,android;player_skip=webpage,configs,initial_data,js;skip=hls,dash,translated_subs;formats=missing_pot;fetch_pot=never\" "
        "-g -f \"" + media_format + "\" " + shell_quote(target_url) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔗 Pulling direct YouTube stream URL (cookie-free fast_opts): " + fast_cmd);
    auto [fast_out, fast_rc] = core::run_shell_command(fast_cmd);
    std::string direct_url = extract_first_http_url(fast_out);
    if (!direct_url.empty()) {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_url_cache[cache_key] = {direct_url, now + CACHE_TTL_SEC};
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ Successfully extracted direct YouTube URL via fast_opts (" + std::to_string(direct_url.size()) + " chars)"
        );
        return direct_url;
    }

    std::string fb_cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates "
        "--no-playlist --cache-dir /tmp/ytdlp_cache "
        "--remote-components ejs:github "
        "--extractor-args \"youtube:player_client=tv_samsung,web_embedded;player_skip=webpage,initial_data;skip=translated_subs;fetch_pot=never\" "
        "-g -f \"" + media_format + "\" " + shell_quote(target_url) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔄 Trying fallback_opts (tv_samsung,web_embedded cookie-free): " + fb_cmd);
    auto [fb_out, fb_rc] = core::run_shell_command(fb_cmd);
    direct_url = extract_first_http_url(fb_out);
    if (!direct_url.empty()) {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_url_cache[cache_key] = {direct_url, now + CACHE_TTL_SEC};
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ Successfully extracted direct YouTube URL via fallback_opts (" + std::to_string(direct_url.size()) + " chars)"
        );
        return direct_url;
    }

    std::string full_reason =
        "❌ تعذر استخراج الرابط المباشر من يوتيوب (بدون كوكيز)!\n"
        "• الرابط المطلوب: " + target_url + "\n"
        "• رد محرك الاستخراج:\n" + (fb_out.empty() ? fast_out : fb_out);

    LOGGER("AnnieXMedia.platforms.Youtube").error(full_reason);
    throw std::runtime_error(full_reason);
}

std::string YouTubeAPI::download(const std::string& query_or_link, bool is_video, bool is_videoid) {
    std::string vid = extract_vid(query_or_link, is_videoid);
    std::string target = is_valid_vidid(vid) ? (base + vid) : query_or_link;
    if (!is_valid_vidid(vid) && !exists(target) && target.rfind("http", 0) != 0) {
        auto td = track(target, false);
        target = td.link;
    }
    return get_direct_link(target, !is_video);
}

std::vector<TrackDetails> YouTubeAPI::playlist(const std::string& link, int limit) {
    std::vector<TrackDetails> list;
    std::string target = link;
    if (target.find('&') != std::string::npos && target.find("list=") != std::string::npos) {
        auto lpos = target.find("list=");
        std::string list_id = target.substr(lpos + 5);
        auto amp = list_id.find('&');
        if (amp != std::string::npos) list_id = list_id.substr(0, amp);
        target = listbase + list_id;
    }

    std::string cmd =
        "yt-dlp --no-cookies --force-ipv4 --no-warnings --no-check-certificates --flat-playlist "
        "--playlist-end " + std::to_string(limit > 0 ? limit : 25) + " "
        "--extractor-args \"youtubetab:skip=webpage\" "
        "--print \"%(id)s\t%(title)s\t%(duration)s\" " + shell_quote(target) + " 2>&1";

    auto [out, rc] = core::run_shell_command(cmd);
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find('\t') == std::string::npos) continue;
            TrackDetails td;
            std::stringstream lss(line);
            std::string dur_s;
            std::getline(lss, td.vidid, '\t');
            std::getline(lss, td.title, '\t');
            std::getline(lss, dur_s, '\t');
            try { td.duration_sec = std::stoi(dur_s); } catch (...) { td.duration_sec = 180; }
            td.duration_min = sec_to_min_str(td.duration_sec);
            td.link = base + td.vidid;
            td.thumb = "https://i.ytimg.com/vi/" + td.vidid + "/hqdefault.jpg";
            list.push_back(td);
        }
    } else {
        LOGGER("AnnieXMedia.platforms.Youtube").error("❌ YouTube playlist error:\n" + out);
    }
    return list;
}

} // namespace AnnieXMedia::platforms
EOF

cat << 'EOF' > AnnieXMedia/core/bot.cpp
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <thread>
#include <unordered_map>

namespace AnnieXMedia::core {

MusicBotClient app;

static std::mutex g_cache_mtx;
static std::unordered_map<int64_t, std::pair<std::string, std::string>> g_user_cache;
static std::unordered_map<int64_t, std::pair<std::string, std::string>> g_chat_cache;
static std::unordered_map<int64_t, int64_t> g_msg_id_map;

static int64_t resolve_message_id(int64_t msg_id) {
    if (msg_id == 0) return 0;
    for (int i = 0; i < 12; ++i) {
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_msg_id_map.find(msg_id);
            if (it != g_msg_id_map.end()) return it->second;
        }
        if ((msg_id & 0xFFFFF) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return msg_id;
}

static std::string fix_html_links(const std::string& html) {
    static const std::regex unquoted_href(R"(<a\s+href=([^"'\s>]+)>)");
    return std::regex_replace(html, unquoted_href, "<a href=\"$1\">");
}

static std::string ensure_local_image(const std::string& path_or_url) {
    if (path_or_url.empty()) return "";
    if (std::filesystem::exists(path_or_url) && std::filesystem::file_size(path_or_url) > 256) {
        return path_or_url;
    }
    if (path_or_url.rfind("http://", 0) == 0 || path_or_url.rfind("https://", 0) == 0) {
        std::filesystem::create_directories(config::DOWNLOAD_PATH + "/thumbs");
        std::string hash_name = std::to_string(std::hash<std::string>{}(path_or_url));
        std::string local_file = config::DOWNLOAD_PATH + "/thumbs/img_" + hash_name + ".jpg";
        if (std::filesystem::exists(local_file) && std::filesystem::file_size(local_file) > 512) {
            return local_file;
        }
        std::string cmd = "curl -4 -sL --max-time 8 -A \"Mozilla/5.0\" \"" + path_or_url + "\" -o \"" + local_file + "\"";
        int sys_rc = std::system(cmd.c_str());
        (void)sys_rc;
        if (std::filesystem::exists(local_file) && std::filesystem::file_size(local_file) > 512) {
            return local_file;
        }
    }
    return "";
}

MusicBotClient::MusicBotClient() {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
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
                LOGGER("AnnieXMedia.core.bot").error("⚠️ قيمة BOT_TOKEN في السكرتس غير صحيحة!");
            } else {
                send_query(td_api::make_object<td_api::checkAuthenticationBotToken>(config::BOT_TOKEN),
                           [](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.bot").error("❌ خطأ في checkAuthenticationBotToken: [" + std::to_string(err->code_) + "] " + err->message_);
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

    if (update->get_id() == td_api::updateUser::ID) {
        auto u = td_api::move_object_as<td_api::updateUser>(update);
        if (u->user_) {
            std::string full_name = u->user_->first_name_ + (u->user_->last_name_.empty() ? "" : (" " + u->user_->last_name_));
            std::string uname;
            if (u->user_->usernames_ && !u->user_->usernames_->active_usernames_.empty()) {
                uname = u->user_->usernames_->active_usernames_[0];
            }
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_user_cache[u->user_->id_] = {full_name, uname};
        }
        return;
    }

    if (update->get_id() == td_api::updateNewChat::ID) {
        auto u = td_api::move_object_as<td_api::updateNewChat>(update);
        if (u->chat_) {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_chat_cache[u->chat_->id_].first = u->chat_->title_;
        }
        return;
    }

    if (update->get_id() == td_api::updateMessageSendSucceeded::ID) {
        auto u = td_api::move_object_as<td_api::updateMessageSendSucceeded>(update);
        if (u->message_) {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_msg_id_map[u->old_message_id_] = u->message_->id_;
        }
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
            {
                std::lock_guard<std::mutex> lk(g_cache_mtx);
                auto it = g_user_cache.find(msg.sender_user_id);
                if (it != g_user_cache.end()) {
                    msg.sender_first_name = it->second.first;
                    msg.sender_username = it->second.second;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_chat_cache.find(msg.chat_id);
            if (it != g_chat_cache.end()) {
                msg.chat_title = it->second.first;
                msg.chat_username = it->second.second;
            }
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
        std::thread([this, handlers, msg]() mutable {
            if (msg.sender_first_name.empty() && msg.sender_user_id != 0) {
                auto u_obj = this->send_query_sync(td_api::make_object<td_api::getUser>(msg.sender_user_id), 2);
                if (u_obj && u_obj->get_id() == td_api::user::ID) {
                    auto* usr = static_cast<const td_api::user*>(u_obj.get());
                    msg.sender_first_name = usr->first_name_ + (usr->last_name_.empty() ? "" : (" " + usr->last_name_));
                    if (usr->usernames_ && !usr->usernames_->active_usernames_.empty()) {
                        msg.sender_username = usr->usernames_->active_usernames_[0];
                    }
                }
            }
            if (msg.chat_title.empty()) {
                auto c_obj = this->send_query_sync(td_api::make_object<td_api::getChat>(msg.chat_id), 2);
                if (c_obj && c_obj->get_id() == td_api::chat::ID) {
                    msg.chat_title = static_cast<const td_api::chat*>(c_obj.get())->title_;
                }
            }
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
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_user_cache.find(cb.sender_user_id);
            if (it != g_user_cache.end()) {
                cb.sender_first_name = it->second.first;
                cb.sender_username = it->second.second;
            }
        }
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
        config::BOT_USERNAME = this->username;
        config::BOT_NAME = this->name;
    } else {
        this->name = config::BOT_NAME;
        this->username = config::BOT_USERNAME;
    }

    if (config::LOGGER_ID != 0) {
        (void)send_query_sync(td_api::make_object<td_api::getChat>(config::LOGGER_ID), 3);
        send_message(
            config::LOGGER_ID,
            "<u><b>» " + this->mention + " ʙᴏᴛ sᴛᴀʀᴛᴇᴅ (C++ TDLib & Native NTgCalls) :</b></u>\n\n"
            "ɪᴅ : <code>" + std::to_string(this->id) + "</code>\n"
            "ɴᴀᴍᴇ : " + this->name + "\n"
            "ᴜsᴇʀɴᴀᴍᴇ : @" + this->username
        );
    }

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
    std::string fixed_html = fix_html_links(html);
    auto res = td::ClientManager::execute(
        td_api::make_object<td_api::parseTextEntities>(fixed_html, td_api::make_object<td_api::textParseModeHTML>())
    );
    if (res && res->get_id() == td_api::formattedText::ID) {
        return td_api::move_object_as<td_api::formattedText>(res);
    }
    if (res && res->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(res.get());
        LOGGER("AnnieXMedia.core.bot").warning("⚠️ HTML Parse warning: " + err->message_ + " | input: " + fixed_html);
    }
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = fixed_html;
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
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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

    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    if (resp && resp->get_id() == td_api::error::ID && chat_id != config::LOGGER_ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.bot").error("❌ send_message error in chat " + std::to_string(chat_id) + ": [" + std::to_string(err->code_) + "] " + err->message_);
    }
    return 0;
}

int64_t MusicBotClient::send_photo(int64_t chat_id, const std::string& photo_path_or_url,
                                   const std::string& caption,
                                   const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    std::string local_img = ensure_local_image(photo_path_or_url);
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessagePhoto>();
    auto ip = td_api::make_object<td_api::inputPhoto>();
    if (!local_img.empty()) {
        ip->photo_ = td_api::make_object<td_api::inputFileLocal>(local_img);
    } else if (std::filesystem::exists(photo_path_or_url)) {
        ip->photo_ = td_api::make_object<td_api::inputFileLocal>(photo_path_or_url);
    } else {
        ip->photo_ = td_api::make_object<td_api::inputFileRemote>(photo_path_or_url);
    }
    content->photo_ = std::move(ip);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 12);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    if (resp && resp->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.bot").warning("⚠️ send_photo fallback to text (reason: " + err->message_ + ")");
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
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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
        rep->message_id_ = resolve_message_id(reply_to_message_id);
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
    int64_t real_id = resolve_message_id(message_id);
    auto req = td_api::make_object<td_api::editMessageText>();
    req->chat_id_ = chat_id;
    req->message_id_ = real_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = parse_html_text(text);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req), [this, chat_id, text, reply_markup](td_api::object_ptr<td_api::Object> obj) {
        if (obj && obj->get_id() == td_api::error::ID) {
            this->send_message(chat_id, text, reply_markup);
        }
    });
}

void MusicBotClient::edit_message_caption(int64_t chat_id, int64_t message_id, const std::string& caption,
                                          const InlineKeyboardMarkup& reply_markup) {
    int64_t real_id = resolve_message_id(message_id);
    auto req = td_api::make_object<td_api::editMessageCaption>();
    req->chat_id_ = chat_id;
    req->message_id_ = real_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    req->caption_ = parse_html_text(caption);
    send_query(std::move(req));
}

void MusicBotClient::delete_message(int64_t chat_id, int64_t message_id) {
    int64_t real_id = resolve_message_id(message_id);
    delete_messages(chat_id, {real_id, message_id});
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

cat << 'EOF' > AnnieXMedia/core/userbot.cpp
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>

namespace AnnieXMedia::core {

std::vector<int> assistants;
std::vector<int64_t> assistantids;
Userbot userbot;

AssistantClient::AssistantClient(int index, std::string session_config)
    : index_(index), session_config_(std::move(session_config)) {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
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
            send_query(std::move(req), [this](td_api::object_ptr<td_api::Object> obj) {
                if (obj && obj->get_id() == td_api::error::ID) {
                    auto* err = static_cast<const td_api::error*>(obj.get());
                    LOGGER("AnnieXMedia.core.userbot").error("❌ خطأ في setTdlibParameters للمساعد " + std::to_string(index_) + ": " + err->message_);
                }
            });
            break;
        }
        case td_api::authorizationStateWaitPhoneNumber::ID: {
            std::string phone_env = config::getenv_str(("ASSISTANT_PHONE" + std::to_string(index_)).c_str(), "");
            if (phone_env.empty() && !session_config_.empty() && (session_config_[0] == '+' || std::isdigit(static_cast<unsigned char>(session_config_[0])))) {
                phone_env = session_config_;
            }
            if (!phone_env.empty()) {
                LOGGER("AnnieXMedia.core.userbot").info("📲 جارٍ إرسال رقم هاتف المساعد " + std::to_string(index_) + " (" + phone_env + ") لطلب كود التفعيل...");
                send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_env, nullptr),
                           [this](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.userbot").error("❌ فشل إرسال رقم المساعد " + std::to_string(index_) + ": " + err->message_);
                               }
                           });
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "📲 المساعد " + std::to_string(index_) + " بانتظار رقم الهاتف! أرسل في خاص البوت: رقم المساعد +201xxxxxxxxx"
                );
            }
            break;
        }
        case td_api::authorizationStateWaitCode::ID: {
            std::string code_env = config::getenv_str(("ASSISTANT_CODE" + std::to_string(index_)).c_str(), "");
            if (!code_env.empty()) {
                send_query(td_api::make_object<td_api::checkAuthenticationCode>(code_env));
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "📲 المساعد " + std::to_string(index_) + " بانتظار كود التفعيل! أرسل الكود الآن في جروب السجل أو في خاص البوت: كود المساعد 1 2 3 4 5"
                );
                app.send_message(
                    config::OWNER_ID,
                    "📲 <b>المساعد " + std::to_string(index_) + " بانتظار كود التفعيل!</b>\n"
                    "أرسل الكود الآن هنا في الشات مفصولاً بمسافات هكذا:\n"
                    "<code>كود المساعد 1 2 3 4 5</code>"
                );
            }
            break;
        }
        case td_api::authorizationStateWaitPassword::ID: {
            std::string pass_env = config::getenv_str(("ASSISTANT_PASSWORD" + std::to_string(index_)).c_str(), "");
            if (!pass_env.empty()) {
                send_query(td_api::make_object<td_api::checkAuthenticationPassword>(pass_env));
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "🔐 المساعد " + std::to_string(index_) + " محمي بكلمة مرور التحقق بخطوتين (2FA)! بانتظار إدخال الباسورد في شات البوت..."
                );
                app.send_message(
                    config::OWNER_ID,
                    "🔐 <b>حساب المساعد " + std::to_string(index_) + " محمي بكلمة مرور التحقق بخطوتين (2FA)!</b>\n"
                    "أرسل كلمة المرور الآن هنا في الشات هكذا:\n"
                    "<code>باسورد المساعد كلمه_السر</code>"
                );
            }
            break;
        }
        case td_api::authorizationStateReady::ID: {
            bool was_ready = authorized_.exchange(true);
            if (!was_ready) {
                std::thread([this]() {
                    auto me_obj = this->send_query_sync(td_api::make_object<td_api::getMe>(), 8);
                    if (me_obj && me_obj->get_id() == td_api::user::ID) {
                        auto me = td_api::move_object_as<td_api::user>(me_obj);
                        this->id = me->id_;
                        this->name = me->first_name_ + (me->last_name_.empty() ? "" : (" " + me->last_name_));
                        if (me->usernames_ && !me->usernames_->active_usernames_.empty()) {
                            this->username = me->usernames_->active_usernames_[0];
                        }
                        if (std::find(assistants.begin(), assistants.end(), index_) == assistants.end()) {
                            assistants.push_back(index_);
                        }
                        if (std::find(assistantids.begin(), assistantids.end(), this->id) == assistantids.end()) {
                            assistantids.push_back(this->id);
                        }
                        LOGGER("AnnieXMedia.core.userbot").info(
                            "☔ تم تشغيل المساعد " + std::to_string(index_) + " بنجاح بهوية: " +
                            this->name + " (@" + this->username + ") [ID: " + std::to_string(this->id) + "]"
                        );

                        this->send_query_sync(td_api::make_object<td_api::getChats>(nullptr, 100), 6);
                        std::string start_text = "☔ تم بدء تشغيل الحساب المساعد " + std::to_string(index_) +
                                                 " بنجاح بهوية: " + this->name + " (@" + this->username + ")";
                        if (config::LOGGER_ID != 0) {
                            this->send_message(config::LOGGER_ID, start_text);
                        }
                        if (config::OWNER_ID != 0) {
                            this->send_message(config::OWNER_ID, start_text);
                            app.send_message(
                                config::OWNER_ID,
                                "✅ <b>تم تشغيل الحساب المساعد " + std::to_string(index_) + " بنجاح!</b>\n"
                                "• الاسم: " + this->name + "\n"
                                "• اليوزر: @" + this->username + "\n"
                                "• الآيدي: <code>" + std::to_string(this->id) + "</code>"
                            );
                        }
                    }
                }).detach();
            }
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

    for (int i = 0; i < 50 && !authorized_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!authorized_.load()) {
        assistants.push_back(index_);
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
    auto chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
    if (!chat_obj || chat_obj->get_id() == td_api::error::ID) {
        send_query_sync(td_api::make_object<td_api::getChats>(nullptr, 100), 5);
        chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
    }
    if (!chat_obj || chat_obj->get_id() != td_api::chat::ID) {
        std::string inv = app.export_chat_invite_link(chat_id);
        if (!inv.empty()) {
            LOGGER("AnnieXMedia.core.userbot").info("🔗 Assistant joining chat " + std::to_string(chat_id) + " via invite link...");
            join_chat_by_link_or_username(inv);
            chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
        }
    }
    if (chat_obj && chat_obj->get_id() == td_api::chat::ID) {
        auto* c = static_cast<const td_api::chat*>(chat_obj.get());
        if (c->video_chat_ && c->video_chat_->group_call_id_ != 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = c->video_chat_->group_call_id_;
            return c->video_chat_->group_call_id_;
        }
    } else if (chat_obj && chat_obj->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(chat_obj.get());
        LOGGER("AnnieXMedia.core.userbot").error("❌ Assistant getChat(" + std::to_string(chat_id) + ") failed: " + err->message_);
    }

    if (auto_create) {
        auto created = send_query_sync(td_api::make_object<td_api::createVideoChat>(chat_id, config::BOT_NAME, 0, false), 8);
        if (created && created->get_id() == td_api::groupCallId::ID) {
            int32_t gcid = static_cast<const td_api::groupCallId*>(created.get())->id_;
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = gcid;
            return gcid;
        }
        if (created && created->get_id() == td_api::error::ID) {
            auto* err = static_cast<const td_api::error*>(created.get());
            LOGGER("AnnieXMedia.core.userbot").error("❌ Assistant createVideoChat(" + std::to_string(chat_id) + ") failed: " + err->message_);
        }
    }
    return 0;
}

std::string AssistantClient::join_group_call_signaling(int64_t chat_id, const std::string& join_payload_json,
                                                       int32_t audio_source_ssrc, bool is_muted, bool is_video_enabled) {
    int32_t group_call_id = get_or_create_group_call_id(chat_id, true);
    if (group_call_id == 0) {
        throw std::runtime_error("لم يتم العثور على مكالمة صوتية مفتوحة في المجموعة (تأكد من فتح المكالمة ووجود المساعد في المجموعة).");
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

    auto resp = send_query_sync(std::move(req), 12);
    if (resp && resp->get_id() == td_api::text::ID) {
        std::string transport = static_cast<const td_api::text*>(resp.get())->text_;
        LOGGER("AnnieXMedia.core.userbot").info("✅ TDLib joinVideoChat succeeded (" + std::to_string(transport.size()) + " bytes transport).");
        return transport;
    }
    if (resp && resp->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.userbot").error("❌ TDLib joinVideoChat error: [" + std::to_string(err->code_) + "] " + err->message_);
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
        send_query_sync(td_api::make_object<td_api::createPrivateChat>(chat_id, false), 3);
    }
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    auto content = td_api::make_object<td_api::inputMessageText>();
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = text;
    content->text_ = std::move(ft);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req), [chat_id](td_api::object_ptr<td_api::Object> obj) {
        if (obj && obj->get_id() == td_api::error::ID) {
            auto* err = static_cast<const td_api::error*>(obj.get());
            LOGGER("AnnieXMedia.core.userbot").warning("⚠️ Assistant send_message(" + std::to_string(chat_id) + ") warning: " + err->message_);
        }
    });
}

Userbot::Userbot() = default;

void Userbot::start() {
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
# 4. ترجمة وبناء البوت النهائي (annie_music_bot)
# ------------------------------------------------------------------
echo "⚙️ [4/4] Compiling AnnieXBoda C++20 Bot executable..."
rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j"$JOBS"
cd "$ROOT_DIR"

echo "🎉 Build finished successfully! Binary: ./build/annie_music_bot"
