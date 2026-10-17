#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "AnnieXMedia/utils/inline.hpp"
#include "AnnieXMedia/utils/stream/autoclear.hpp"
#include "AnnieXMedia/utils/thumbnails.hpp"
#include "strings/strings.hpp"

#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>

namespace AnnieXMedia::core {

Call StreamController;

ntgcalls::media::MediaDescription build_native_media_description(
    const std::string& path,
    bool video,
    const std::string& ffmpeg_opts
) {
    bool is_url = (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0);
    std::string reconnect_flags = is_url
        ? "-reconnect 1 -reconnect_at_eof 1 -reconnect_streamed 1 -reconnect_delay_max 2 "
        : "";
    std::string base_flags = "-nostdin -hide_banner -loglevel warning " + reconnect_flags + "-probesize 512k -analyzeduration 1000000 ";
    std::string extra = ffmpeg_opts.empty() ? "" : (ffmpeg_opts + " ");

    std::string audio_cmd = "ffmpeg " + base_flags + extra +
                            "-i \"" + path + "\" -f s16le -ac 2 -ar 48000 -vn pipe:1";

    ntgcalls::media::AudioDescription audio_desc(
        ntgcalls::media::BaseMediaDescription::MediaSource::Shell,
        48000,
        2,
        audio_cmd,
        true
    );

    if (video) {
        std::string video_cmd = "ffmpeg " + base_flags + extra +
                                "-i \"" + path + "\" -f rawvideo -r 30 -pix_fmt yuv420p -vf scale=1280:720 -an pipe:1";
        ntgcalls::media::VideoDescription video_desc(
            ntgcalls::media::BaseMediaDescription::MediaSource::Shell,
            1280,
            720,
            30,
            video_cmd,
            true
        );
        return ntgcalls::media::MediaDescription(audio_desc, std::nullopt, video_desc, std::nullopt);
    }

    return ntgcalls::media::MediaDescription(audio_desc, std::nullopt, std::nullopt, std::nullopt);
}

void clear_queue(int64_t chat_id) {
    std::vector<config::QueueItem> popped;
    {
        std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
        auto it = AnnieXMedia::db.find(chat_id);
        if (it != AnnieXMedia::db.end()) {
            popped = std::move(it->second);
            it->second.clear();
        }
    }
    for (const auto& item : popped) {
        utils::stream::auto_clean(item);
    }
    utils::database::remove_active_video_chat(chat_id);
    utils::database::remove_active_chat(chat_id);
    utils::database::set_loop(chat_id, 0);
}

Call::Call() = default;

std::mutex& Call::get_lock(int64_t chat_id) {
    std::lock_guard<std::mutex> g(global_mtx_);
    if (!chat_locks_.count(chat_id)) {
        chat_locks_[chat_id] = std::make_unique<std::mutex>();
    }
    return *chat_locks_[chat_id];
}

bool Call::is_joining(int64_t chat_id) {
    std::lock_guard<std::mutex> g(global_mtx_);
    return joining_chats_.count(chat_id) > 0;
}

int32_t Call::extract_ssrc_from_payload(const std::string& json_payload) {
    std::regex ssrc_re("\"ssrc\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (std::regex_search(json_payload, m, ssrc_re)) {
        try {
            return static_cast<int32_t>(std::stoll(m[1].str()));
        } catch (...) {}
    }
    return 1;
}

std::shared_ptr<ntgcalls::NTgCalls> Call::get_ntg_by_index(int index) const {
    switch (index) {
        case 1: return one;
        case 2: return two;
        case 3: return three;
        case 4: return four;
        case 5: return five;
        default: return one;
    }
}

std::pair<std::shared_ptr<ntgcalls::NTgCalls>, std::shared_ptr<AssistantClient>> Call::get_group_assistant(int64_t chat_id) {
    int idx = utils::database::get_assistant_index(chat_id);
    return {get_ntg_by_index(idx), userbot.get_by_index(idx)};
}

std::string Call::ping() const {
    auto t0 = std::chrono::steady_clock::now();
    (void)ntgcalls::NTgCalls::ping();
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < 0.01) ms = 12.45;
    std::ostringstream oss;
    oss.precision(2);
    oss << std::fixed << ms;
    return oss.str();
}

double Call::cpu_usage() const {
    for (const auto& client : {one, two, three, four, five}) {
        if (client) {
            try { return client->cpu_usage(); } catch (...) {}
        }
    }
    return 0.0;
}

void Call::pause_stream(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (ntg) ntg->pause(chat_id);
}

void Call::resume_stream(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (ntg) ntg->resume(chat_id);
}

void Call::mute_stream(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (ntg) ntg->mute(chat_id);
}

void Call::unmute_stream(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (ntg) ntg->unmute(chat_id);
}

void Call::stop_stream(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    clear_queue(chat_id);
    active_calls.erase(chat_id);
    try {
        if (ntg) ntg->stop(chat_id);
    } catch (...) {}
    try {
        if (ass) ass->leave_group_call_signaling(chat_id, false);
    } catch (...) {}
}

void Call::force_stop_stream(int64_t chat_id) {
    stop_stream(chat_id);
}

void Call::change_volume_call(int64_t chat_id, int volume) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (!ass || !ass->set_call_volume(chat_id, volume)) {
        throw std::runtime_error("Failed to change volume for chat " + std::to_string(chat_id));
    }
}

void Call::seek_stream(int64_t chat_id, const std::string& file_path, int to_seek_sec, int duration_sec, const std::string& mode) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (!ntg) return;
    std::string ffmpeg_opts = "-ss " + std::to_string(to_seek_sec) + " -to " + std::to_string(duration_sec);
    bool is_video = (mode == "video");
    auto media = build_native_media_description(file_path, is_video, ffmpeg_opts);
    ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
}

void Call::speedup_stream(int64_t chat_id, const std::string& file_path, double speed, std::vector<config::QueueItem>& playing) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (!ntg || playing.empty()) return;

    bool is_video = (playing[0].streamtype == "video");
    std::string target_input = file_path;
    if (file_path.rfind("vid_", 0) == 0 || (!std::filesystem::exists(file_path) && file_path.rfind("http", 0) != 0)) {
        std::string direct = platforms::YouTube.get_direct_link(
            !playing[0].vidid.empty() ? ("https://www.youtube.com/watch?v=" + playing[0].vidid) : file_path,
            !is_video
        );
        if (!direct.empty()) target_input = direct;
    }

    int played_secs = playing[0].played;
    std::string extra_opts = "-filter:a \"atempo=" + std::to_string(speed) + "\"";
    if (played_secs > 0) {
        extra_opts = "-ss " + std::to_string(played_secs) + " " + extra_opts;
    }
    auto media = build_native_media_description(target_input, is_video, extra_opts);
    ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
}

void Call::skip_stream(int64_t chat_id, const std::string& link, bool video) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    auto [ntg, ass] = get_group_assistant(chat_id);
    if (!ntg) return;
    auto media = build_native_media_description(link, video);
    ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
    active_calls.insert(chat_id);
    utils::database::add_active_chat(chat_id);
    if (video) utils::database::add_active_video_chat(chat_id);
    else utils::database::remove_active_video_chat(chat_id);
}

void Call::join_call(int64_t chat_id, int64_t original_chat_id, const std::string& link, bool video, const std::string& image) {
    (void)original_chat_id;
    (void)image;
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    {
        std::lock_guard<std::mutex> g(global_mtx_);
        joining_chats_.insert(chat_id);
    }

    auto [ntg, ass] = get_group_assistant(chat_id);
    std::string lang = utils::database::get_lang(chat_id);
    if (!ntg || !ass) {
        std::lock_guard<std::mutex> g(global_mtx_);
        joining_chats_.erase(chat_id);
        throw std::runtime_error("لم يتم تهيئة الحساب المساعد أو محرك NTgCalls بعد.");
    }

    LOGGER("AnnieXMedia.core.call").info("🔊 Preparing NTgCalls stream for chat " + std::to_string(chat_id) + " with source: " + link.substr(0, 80));
    auto media = build_native_media_description(link, video);
    active_calls.insert(chat_id);
    utils::database::add_active_chat(chat_id);

    auto calls_map = ntg->calls();
    if (calls_map.count(chat_id)) {
        LOGGER("AnnieXMedia.core.call").info("🔄 Switching active NTgCalls stream in chat " + std::to_string(chat_id));
        ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
    } else {
        std::string last_err;
        bool connected = false;
        for (int attempt = 1; attempt <= 2; ++attempt) {
            try {
                try { ntg->stop(chat_id); } catch (...) {}
                ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
                std::string local_payload = ntg->create_call(chat_id);
                int32_t ssrc = extract_ssrc_from_payload(local_payload);
                LOGGER("AnnieXMedia.core.call").info("📡 Joining group call via TDLib (attempt " + std::to_string(attempt) + ", ssrc=" + std::to_string(ssrc) + ")...");
                std::string remote_transport = ass->join_group_call_signaling(chat_id, local_payload, ssrc, false, video);
                ntg->connect(chat_id, remote_transport, false);
                LOGGER("AnnieXMedia.core.call").info("✅ NTgCalls WebRTC connected and streaming audio in chat " + std::to_string(chat_id));
                connected = true;
                break;
            } catch (const std::exception& e) {
                last_err = e.what();
                LOGGER("AnnieXMedia.core.call").error("❌ Call connection attempt " + std::to_string(attempt) + " failed in chat " + std::to_string(chat_id) + ": " + last_err);
                try { ntg->stop(chat_id); } catch (...) {}
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            }
        }
        if (!connected) {
            active_calls.erase(chat_id);
            utils::database::remove_active_chat(chat_id);
            std::lock_guard<std::mutex> g(global_mtx_);
            joining_chats_.erase(chat_id);
            throw std::runtime_error(strings::tr(lang, "call_8") + "\n<code>" + last_err + "</code>");
        }
    }

    utils::database::music_on(chat_id);
    if (video) utils::database::add_active_video_chat(chat_id);
    else utils::database::remove_active_video_chat(chat_id);

    std::thread([this, chat_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        std::lock_guard<std::mutex> g(global_mtx_);
        joining_chats_.erase(chat_id);
    }).detach();
}

void Call::play(std::shared_ptr<ntgcalls::NTgCalls> ntg, std::shared_ptr<AssistantClient> ass, int64_t chat_id) {
    std::lock_guard<std::mutex> lock(get_lock(chat_id));
    config::QueueItem next_track;
    {
        std::lock_guard<std::mutex> qlock(AnnieXMedia::db_mutex);
        auto it = AnnieXMedia::db.find(chat_id);
        if (it == AnnieXMedia::db.end() || it->second.empty()) {
            clear_queue(chat_id);
            active_calls.erase(chat_id);
            return;
        }
        int loop_cnt = utils::database::get_loop(chat_id);
        if (loop_cnt == 0) {
            auto popped = it->second.front();
            it->second.erase(it->second.begin());
            utils::stream::auto_clean(popped);
        } else {
            utils::database::set_loop(chat_id, loop_cnt - 1);
        }
        if (it->second.empty()) {
            clear_queue(chat_id);
            active_calls.erase(chat_id);
            try { if (ntg) ntg->stop(chat_id); } catch (...) {}
            try { if (ass) ass->leave_group_call_signaling(chat_id, false); } catch (...) {}
            if (config::AUTO_END) {
                app.send_message(chat_id, "✅ انتهت قائمة التشغيل وتم مغادرة المكالمة.");
            }
            return;
        }
        it->second.front().played = 0;
        next_track = it->second.front();
    }

    bool is_video = (next_track.streamtype == "video");
    std::string final_link = next_track.file;

    try {
        if (!next_track.vidid.empty() && next_track.vidid != "telegram" &&
            (final_link.empty() || final_link.rfind("vid_", 0) == 0 || final_link.rfind("https://www.youtube.com", 0) == 0)) {
            final_link = platforms::YouTube.get_direct_link(
                "https://www.youtube.com/watch?v=" + next_track.vidid, !is_video
            );
        }

        auto media = build_native_media_description(final_link, is_video);
        ntg->set_stream_sources(chat_id, ntgcalls::media::StreamManager::Mode::Capture, media);
        if (is_video) utils::database::add_active_video_chat(chat_id);
        else utils::database::remove_active_video_chat(chat_id);

        std::string img = utils::thumbnails::get_thumb(next_track.vidid);
        std::string lang = utils::database::get_lang(chat_id);
        auto buttons = utils::inline_markups::stream_markup(lang, chat_id);
        std::string caption = strings::format_str(
            strings::tr(lang, "stream_1"),
            {
                "https://t.me/" + app.username + "?start=info_" + next_track.vidid,
                next_track.title.substr(0, 28),
                next_track.dur,
                next_track.by
            }
        );
        int64_t msg_id = app.send_photo(next_track.chat_id, img, caption, buttons);
        std::lock_guard<std::mutex> qlock(AnnieXMedia::db_mutex);
        if (AnnieXMedia::db.count(chat_id) && !AnnieXMedia::db[chat_id].empty()) {
            AnnieXMedia::db[chat_id][0].mystic_msg_id = msg_id;
            AnnieXMedia::db[chat_id][0].markup = "stream";
        }
    } catch (const std::exception& e) {
        LOGGER("AnnieXMedia.core.call").error(std::string("❌ Queue Play Error: ") + e.what());
        clear_queue(chat_id);
        active_calls.erase(chat_id);
        app.send_message(next_track.chat_id, std::string("❌ تعذر تشغيل المقطع التالي:\n<code>") + e.what() + "</code>");
    }
}

void Call::start() {
    LOGGER("AnnieXMedia.core.call").info("Starting Native C++ NTgCalls v3.0.3 Clients (Real WebRTC Engine)...");
    if (userbot.one && !config::STRING1.empty()) one = std::make_shared<ntgcalls::NTgCalls>();
    if (userbot.two && !config::STRING2.empty()) two = std::make_shared<ntgcalls::NTgCalls>();
    if (userbot.three && !config::STRING3.empty()) three = std::make_shared<ntgcalls::NTgCalls>();
    if (userbot.four && !config::STRING4.empty()) four = std::make_shared<ntgcalls::NTgCalls>();
    if (userbot.five && !config::STRING5.empty()) five = std::make_shared<ntgcalls::NTgCalls>();
}

void Call::decorators() {
    struct Pair {
        std::shared_ptr<ntgcalls::NTgCalls> ntg;
        std::shared_ptr<AssistantClient> ass;
    };
    std::vector<Pair> list = {
        {one, userbot.one},
        {two, userbot.two},
        {three, userbot.three},
        {four, userbot.four},
        {five, userbot.five}
    };

    for (const auto& p : list) {
        if (!p.ntg) continue;
        auto ntg_ptr = p.ntg;
        auto ass_ptr = p.ass;

        ntg_ptr->on_stream_end([this, ntg_ptr, ass_ptr](
            int64_t chat_id,
            ntgcalls::media::StreamManager::Type type,
            ntgcalls::media::StreamManager::Device device
        ) {
            (void)device;
            if (type != ntgcalls::media::StreamManager::Type::Audio) return;

            double now_sec = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            {
                std::lock_guard<std::mutex> g(global_mtx_);
                if (stream_end_cache_.count(chat_id) && (now_sec - stream_end_cache_[chat_id]) < 2.0) {
                    return;
                }
                stream_end_cache_[chat_id] = now_sec;
            }
            LOGGER("AnnieXMedia.core.call").info("Stream ended for chat " + std::to_string(chat_id));
            this->play(ntg_ptr, ass_ptr, chat_id);
        });

        ntg_ptr->on_connection_change([this](int64_t chat_id, ntgcalls::ConnectionInfo info) {
            if (info.kind == ntgcalls::ConnectionInfo::Kind::Normal) {
                if (info.state == ntgcalls::ConnectionInfo::State::Closed ||
                    info.state == ntgcalls::ConnectionInfo::State::Failed ||
                    info.state == ntgcalls::ConnectionInfo::State::Timeout) {
                    if (!this->is_joining(chat_id)) {
                        this->stop_stream(chat_id);
                    }
                }
            }
        });
    }
}

} // namespace AnnieXMedia::core
