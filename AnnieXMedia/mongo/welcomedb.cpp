#include "AnnieXMedia/mongo/welcomedb.hpp"
#include "AnnieXMedia/core/mongo.hpp"

namespace AnnieXMedia::mongo {
bool is_welcome_on(int64_t chat_id) {
    auto doc = core::mongodb.collection("welcome")->find_one(std::to_string(chat_id));
    if (!doc) return true;
    return doc->get_str("state", "on") == "on";
}

void set_welcome_state(int64_t chat_id, const std::string& flag) {
    core::mongodb.collection("welcome")->set_field(std::to_string(chat_id), "state", flag);
}
} // namespace AnnieXMedia::mongo
