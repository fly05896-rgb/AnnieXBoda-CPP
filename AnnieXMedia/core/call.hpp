// Authored By Certified Coders © 2026
// System: Direct Native C++ NTgCalls v3.0 & TDLib Call Controller (NO PyTgCalls Wrapper)
// Converted from AnnieXMedia/core/call.py to C++20
#pragma once

#include <ntgcalls/ntgcalls.hpp>
#include <ntgcalls/media/media_description.hpp>
#include <ntgcalls/media/stream_manager.hpp>
#include <ntgcalls/exceptions.hpp>

#include "AnnieXMedia/core/userbot.hpp"
#include "config.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace AnnieXMedia::core {

// Build native ntgcalls::media::MediaDescription using FFmpeg shell pipes (48kHz stereo s16le + 720p30 yuv420p)
ntgcalls::media::MediaDescription build_native_media_description(
    const std::string& path,
    bool video = false,
    const std::string& ffmpeg_opts = ""
);

void clear_queue(int64_t chat_id);

class Call {
    std::mutex global_mtx_;
    std::unordered_map<int64_t, std::unique_ptr<std::mutex>> chat_locks_;
    std::unordered_map<int64_t, double> stream_end_cache_;
    std::set<int64_t> joining_chats_;

    // Extract SSRC integer from NTgCalls::create_call() JSON payload
    static int32_t extract_ssrc_from_payload(const std::string& json_payload);

public:
    // Native C++ ntgcalls::NTgCalls instances (1 per assistant, NO pytgcalls!)
    std::shared_ptr<ntgcalls::NTgCalls> one;
    std::shared_ptr<ntgcalls::NTgCalls> two;
    std::shared_ptr<ntgcalls::NTgCalls> three;
    std::shared_ptr<ntgcalls::NTgCalls> four;
    std::shared_ptr<ntgcalls::NTgCalls> five;

    std::set<int64_t> active_calls;

    Call();
    ~Call() = default;

    std::mutex& get_lock(int64_t chat_id);
    bool is_joining(int64_t chat_id);

    std::shared_ptr<ntgcalls::NTgCalls> get_ntg_by_index(int index) const;
    std::pair<std::shared_ptr<ntgcalls::NTgCalls>, std::shared_ptr<AssistantClient>> get_group_assistant(int64_t chat_id);

    std::string ping() const;
    double cpu_usage() const;

    void pause_stream(int64_t chat_id);
    void resume_stream(int64_t chat_id);
    void mute_stream(int64_t chat_id);
    void unmute_stream(int64_t chat_id);
    void stop_stream(int64_t chat_id);
    void force_stop_stream(int64_t chat_id);
    void change_volume_call(int64_t chat_id, int volume);

    void seek_stream(int64_t chat_id, const std::string& file_path, int to_seek_sec, int duration_sec, const std::string& mode);
    void speedup_stream(int64_t chat_id, const std::string& file_path, double speed, std::vector<config::QueueItem>& playing);
    void skip_stream(int64_t chat_id, const std::string& link, bool video = false);

    void join_call(int64_t chat_id, int64_t original_chat_id, const std::string& link, bool video = false, const std::string& image = "");
    void play(std::shared_ptr<ntgcalls::NTgCalls> ntg, std::shared_ptr<AssistantClient> ass, int64_t chat_id);

    void start();
    void decorators();
};

extern Call StreamController;

} // namespace AnnieXMedia::core
