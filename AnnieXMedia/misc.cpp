#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/logging.hpp"
#include <unistd.h>

namespace AnnieXMedia {

std::unordered_set<int64_t> SUDOERS;
std::unordered_map<int64_t, std::vector<config::QueueItem>> db;
std::mutex db_mutex;
std::time_t _boot_ = std::time(nullptr);

bool is_heroku() {
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        std::string h(hostname);
        return h.find("heroku") != std::string::npos;
    }
    return false;
}

bool is_sudo(int64_t user_id) {
    return user_id == config::OWNER_ID || SUDOERS.count(user_id) > 0;
}

void dbb() {
    std::lock_guard<std::mutex> lock(db_mutex);
    db.clear();
    LOGGER("AnnieXMedia.misc").info("ᴅᴀᴛᴀʙᴀsᴇ ʟᴏᴀᴅᴇᴅ sᴜᴄᴄᴇssғᴜʟʟʏ💗");
}

void sudo() {
    SUDOERS.insert(config::OWNER_ID);
    auto sudoersdb = core::mongodb.collection("sudoers");
    auto doc = sudoersdb->find_one("sudo");
    std::vector<int64_t> list;
    if (doc && doc->int_lists.count("sudoers")) {
        list = doc->int_lists.at("sudoers");
    }
    bool found_owner = false;
    for (auto id : list) {
        SUDOERS.insert(id);
        if (id == config::OWNER_ID) found_owner = true;
    }
    if (!found_owner) {
        list.push_back(config::OWNER_ID);
        sudoersdb->set_int_list("sudo", "sudoers", list);
    }
    LOGGER("AnnieXMedia.misc").info("sᴜᴅᴏ ᴜsᴇʀs ᴅᴏɴᴇ..");
}

void heroku() {
    if (is_heroku() && !config::HEROKU_API_KEY.empty() && !config::HEROKU_APP_NAME.empty()) {
        LOGGER("AnnieXMedia.misc").info("ʜᴇʀᴏᴋᴜ ᴀᴘᴘ ᴄᴏɴғɪɢᴜʀᴇᴅ..");
    }
}

} // namespace AnnieXMedia
