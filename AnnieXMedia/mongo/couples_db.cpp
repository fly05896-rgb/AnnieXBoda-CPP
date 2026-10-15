#include "AnnieXMedia/mongo/couples_db.hpp"
#include "AnnieXMedia/core/mongo.hpp"

namespace AnnieXMedia::mongo {
std::optional<CoupleData> get_couple(int64_t cid, const std::string& date) {
    auto col = core::mongodb.collection("couple");
    std::string key = std::to_string(cid) + "_" + date;
    auto doc = col->find_one(key);
    if (!doc) return std::nullopt;
    CoupleData d;
    d.user1 = doc->get_int("user1", 0);
    d.user2 = doc->get_int("user2", 0);
    d.img = doc->get_str("img", "");
    if (d.user1 == 0 || d.user2 == 0) return std::nullopt;
    return d;
}

void save_couple(int64_t cid, const std::string& date, int64_t u1, int64_t u2, const std::string& img_path) {
    auto col = core::mongodb.collection("couple");
    std::string key = std::to_string(cid) + "_" + date;
    core::Doc doc;
    doc.fields["user1"] = std::to_string(u1);
    doc.fields["user2"] = std::to_string(u2);
    doc.fields["img"] = img_path;
    col->update_one(key, doc, true);
}
} // namespace AnnieXMedia::mongo
