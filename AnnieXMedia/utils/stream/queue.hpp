#pragma once
#include <cstdint>
#include <string>

namespace AnnieXMedia::utils::stream {

void put_queue(
    int64_t chat_id,
    int64_t original_chat_id,
    const std::string& file,
    const std::string& title,
    const std::string& duration,
    const std::string& user,
    const std::string& vidid,
    int64_t user_id,
    const std::string& stream_type,
    bool forceplay = false
);

void put_queue_index(
    int64_t chat_id,
    int64_t original_chat_id,
    const std::string& file,
    const std::string& title,
    const std::string& duration,
    const std::string& user,
    const std::string& vidid,
    const std::string& stream_type,
    bool forceplay = false
);

} // namespace AnnieXMedia::utils::stream
