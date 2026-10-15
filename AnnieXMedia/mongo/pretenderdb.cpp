#include "AnnieXMedia/mongo/pretenderdb.hpp"
#include "AnnieXMedia/core/mongo.hpp"

namespace AnnieXMedia::mongo {
bool usr_data(int64_t user_id) {
    return core::mongodb.collection("pretender")->find_one("u_" + std::to_string(user_id)).has_value();
}

std::tuple<std::string, std::string, std::string> get_userdata(int64_t user_id) {
    auto doc = core::mongodb.collection("pretender")->find_one("u_" + std::to_string(user_id));
    if (!doc) return {"", "", ""};
    return {doc->get_str("username"), doc->get_str("first_name"), doc->get_str("last_name")};
}

void add_userdata(int64_t user_id, const std::string& username, const std::string& first_name, const std::string& last_name) {
    core::Doc d;
    d.fields["username"] = username;
    d.fields["first_name"] = first_name;
    d.fields["last_name"] = last_name;
    core::mongodb.collection("pretender")->update_one("u_" + std::to_string(user_id), d, true);
}

bool check_pretender(int64_t chat_id) {
    return core::mongodb.collection("pretender")->find_one("c_" + std::to_string(chat_id)).has_value();
}

void impo_on(int64_t chat_id) {
    core::mongodb.collection("pretender")->set_field("c_" + std::to_string(chat_id), "enabled", "1");
}

void impo_off(int64_t chat_id) {
    core::mongodb.collection("pretender")->delete_one("c_" + std::to_string(chat_id));
}
} // namespace AnnieXMedia::mongo
