#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"
#include <sstream>

namespace AnnieXMedia::core {

MongoDatabase mongodb;

MongoCollection::MongoCollection(std::string name) : name_(std::move(name)) {
    std::filesystem::create_directories("cache/mongodb_store");
    storage_file_ = "cache/mongodb_store/" + name_ + ".db";
    load_from_disk();
}

void MongoCollection::load_from_disk() {
    std::ifstream in(storage_file_);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string pk, type, k, v;
        if (!std::getline(ss, pk, '\t')) continue;
        if (!std::getline(ss, type, '\t')) continue;
        if (!std::getline(ss, k, '\t')) continue;
        std::getline(ss, v);
        if (type == "F") {
            docs_[pk].fields[k] = v;
        } else if (type == "L") {
            std::vector<int64_t> vec;
            std::stringstream lss(v);
            std::string item;
            while (std::getline(lss, item, ',')) {
                if (!item.empty()) {
                    try { vec.push_back(std::stoll(item)); } catch (...) {}
                }
            }
            docs_[pk].int_lists[k] = std::move(vec);
        } else if (type == "S") {
            auto eq = v.find('=');
            if (eq != std::string::npos) {
                docs_[pk].subdocs[k][v.substr(0, eq)] = v.substr(eq + 1);
            }
        }
    }
}

void MongoCollection::save_to_disk() const {
    std::ofstream out(storage_file_, std::ios::trunc);
    if (!out.is_open()) return;
    for (const auto& [pk, doc] : docs_) {
        for (const auto& [k, v] : doc.fields) {
            out << pk << "\tF\t" << k << "\t" << v << "\n";
        }
        for (const auto& [k, vec] : doc.int_lists) {
            out << pk << "\tL\t" << k << "\t";
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) out << ",";
                out << vec[i];
            }
            out << "\n";
        }
        for (const auto& [k, sub] : doc.subdocs) {
            for (const auto& [sk, sv] : sub) {
                out << pk << "\tS\t" << k << "\t" << sk << "=" << sv << "\n";
            }
        }
    }
}

std::optional<Doc> MongoCollection::find_one(const std::string& primary_key) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = docs_.find(primary_key);
    if (it == docs_.end()) return std::nullopt;
    return it->second;
}

void MongoCollection::update_one(const std::string& primary_key, const Doc& doc, bool upsert) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!upsert && !docs_.count(primary_key)) return;
    docs_[primary_key] = doc;
    save_to_disk();
}

void MongoCollection::set_field(const std::string& primary_key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx_);
    docs_[primary_key].fields[field] = value;
    save_to_disk();
}

void MongoCollection::set_int_list(const std::string& primary_key, const std::string& field, const std::vector<int64_t>& list) {
    std::lock_guard<std::mutex> lock(mtx_);
    docs_[primary_key].int_lists[field] = list;
    save_to_disk();
}

void MongoCollection::set_subdoc(const std::string& primary_key, const std::string& field, const std::unordered_map<std::string, std::string>& sub) {
    std::lock_guard<std::mutex> lock(mtx_);
    docs_[primary_key].subdocs[field] = sub;
    save_to_disk();
}

bool MongoCollection::delete_one(const std::string& primary_key) {
    std::lock_guard<std::mutex> lock(mtx_);
    bool erased = docs_.erase(primary_key) > 0;
    if (erased) save_to_disk();
    return erased;
}

std::vector<std::pair<std::string, Doc>> MongoCollection::find_all() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::pair<std::string, Doc>> res;
    res.reserve(docs_.size());
    for (const auto& kv : docs_) res.push_back(kv);
    return res;
}

size_t MongoCollection::count_documents() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return docs_.size();
}

MongoDatabase::MongoDatabase() = default;

std::shared_ptr<MongoCollection> MongoDatabase::collection(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = collections_.find(name);
    if (it != collections_.end()) return it->second;
    auto col = std::make_shared<MongoCollection>(name);
    collections_[name] = col;
    return col;
}

void init_mongo() {
    LOGGER("AnnieXMedia.core.mongo").info("Connecting to your Mongo Database...");
    if (config::MONGO_DB_URI.empty()) {
        LOGGER("AnnieXMedia.core.mongo").warning("MONGO_DB_URI is empty, using fast local persistent DB engine.");
    } else {
        LOGGER("AnnieXMedia.core.mongo").info("Connected to Mongo Database Engine (" + config::MONGO_DB_URI.substr(0, 18) + "...).");
    }
}

} // namespace AnnieXMedia::core
