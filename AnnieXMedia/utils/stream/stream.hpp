#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <cstdint>
#include <string>

namespace AnnieXMedia::utils::stream {

void start_stream(
    const std::string& lang,
    int64_t mystic_msg_id,
    int64_t user_id,
    const platforms::TrackDetails& details,
    int64_t chat_id,
    const std::string& user_name,
    int64_t original_chat_id,
    bool video = false,
    const std::string& streamtype = "youtube",
    bool forceplay = false
);

} // namespace AnnieXMedia::utils::stream
