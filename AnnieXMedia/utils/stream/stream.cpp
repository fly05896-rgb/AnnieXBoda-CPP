#include "AnnieXMedia/utils/stream/stream.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/utils/inline.hpp"
#include "AnnieXMedia/utils/stream/queue.hpp"
#include "AnnieXMedia/utils/thumbnails.hpp"
#include "strings/strings.hpp"

namespace AnnieXMedia::utils::stream {

void start_stream(
    const std::string& lang,
    int64_t mystic_msg_id,
    int64_t user_id,
    const platforms::TrackDetails& details,
    int64_t chat_id,
    const std::string& user_name,
    int64_t original_chat_id,
    bool video,
    const std::string& streamtype,
    bool forceplay
) {
    if (forceplay) {
        core::StreamController.force_stop_stream(chat_id);
    }

    std::string file_path = details.file_path;
    if (file_path.empty()) {
        if (streamtype == "live" || streamtype == "index") {
            file_path = platforms::YouTube.get_direct_link(details.link, !video);
        } else {
            file_path = platforms::YouTube.download(details.link, video, false);
        }
    }

    bool active = database::is_active_chat(chat_id);
    std::string mode_str = video ? "video" : "audio";

    if (active && !forceplay) {
        put_queue(
            chat_id,
            original_chat_id,
            file_path,
            details.title,
            details.duration_min,
            user_name,
            details.vidid,
            user_id,
            mode_str,
            false
        );
        size_t pos = 1;
        {
            std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
            if (AnnieXMedia::db.count(chat_id) && AnnieXMedia::db[chat_id].size() > 1) {
                pos = AnnieXMedia::db[chat_id].size() - 1;
            }
        }
        std::string q_text = strings::format_str(
            strings::tr(lang, "queue_4"),
            {std::to_string(pos), details.title.substr(0, 27), details.duration_min, user_name}
        );
        core::app.edit_message_text(original_chat_id, mystic_msg_id, q_text, inline_markups::aq_markup(lang, chat_id));
        return;
    }

    // Join voice/video call via Native C++ NTgCalls
    core::StreamController.join_call(chat_id, original_chat_id, file_path, video, details.thumb);

    put_queue(
        chat_id,
        original_chat_id,
        file_path,
        details.title,
        details.duration_min,
        user_name,
        details.vidid,
        user_id,
        mode_str,
        forceplay
    );

    std::string img = thumbnails::get_thumb(details.vidid);
    auto buttons = inline_markups::stream_markup(lang, chat_id);
    std::string caption = strings::format_str(
        strings::tr(lang, "stream_1"),
        {
            "https://t.me/" + core::app.username + "?start=info_" + details.vidid,
            details.title.substr(0, 23),
            details.duration_min,
            user_name
        }
    );

    core::app.delete_message(original_chat_id, mystic_msg_id);
    int64_t run_id = core::app.send_photo(original_chat_id, img, caption, buttons);

    std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
    if (AnnieXMedia::db.count(chat_id) && !AnnieXMedia::db[chat_id].empty()) {
        AnnieXMedia::db[chat_id][0].mystic_msg_id = run_id;
        AnnieXMedia::db[chat_id][0].markup = "stream";
    }
}

} // namespace AnnieXMedia::utils::stream
