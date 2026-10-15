#include "AnnieXMedia/utils/stream/queue.hpp"
#include "AnnieXMedia/misc.hpp"
#include "config.hpp"

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
    bool forceplay
) {
    int dur_sec = std::max(0, config::time_to_seconds(duration) - 3);
    config::QueueItem item;
    item.title = title;
    item.dur = duration;
    item.streamtype = stream_type;
    item.by = user;
    item.user_id = user_id;
    item.chat_id = original_chat_id;
    item.file = file;
    item.vidid = vidid;
    item.seconds = dur_sec;
    item.played = 0;

    std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
    auto& q = AnnieXMedia::db[chat_id];
    if (forceplay) {
        q.insert(q.begin(), item);
    } else {
        q.push_back(item);
    }
}

void put_queue_index(
    int64_t chat_id,
    int64_t original_chat_id,
    const std::string& file,
    const std::string& title,
    const std::string& duration,
    const std::string& user,
    const std::string& vidid,
    const std::string& stream_type,
    bool forceplay
) {
    put_queue(chat_id, original_chat_id, file, title, duration, user, vidid, 0, stream_type, forceplay);
}

} // namespace AnnieXMedia::utils::stream
