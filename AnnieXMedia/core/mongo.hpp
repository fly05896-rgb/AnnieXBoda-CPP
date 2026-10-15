#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace AnnieXMedia::core {

// High-speed thread-safe Document & Collection store compatible with MongoDB collections
// Persists locally in data/mongodb_store/ and optionally syncs with MONGO_DB_URI.
struct Doc {
    std::unordered_map<std::string, std::string> fields;
    std::unordered_map<std::string, std::vector<int64_t>> int_lists;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> subdocs;

    std::string get_str(const std::string& k, const std::string& def = "") const {
        auto it = fields.find(k);
        return it != fields.end() ? it->second : def;
    }
    int64_t get_int(const std::string& k, int64_t def = 0) const {
        auto it = fields.find(k);
        if (it == fields.end()) return def;
        try { return std::stoll(it->second); } catch (...) { return def; }
    }
    bool get_bool(const std::string& k, bool def = false) const {
        auto it = fields.find(k);
        if (it == fields.end()) return def;
        return it->second == "1" || it->second == "true" || it->second == "True";
    }
};

class MongoCollection {
    std::string name_;
    std::string storage_file_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, Doc> docs_;

    void load_from_disk();
    void save_to_disk() const;

public:
    explicit MongoCollection(std::string name);

    std::optional<Doc> find_one(const std::string& primary_key) const;
    void update_one(const std::string& primary_key, const Doc& doc, bool upsert = true);
    void set_field(const std::string& primary_key, const std::string& field, const std::string& value);
    void set_int_list(const std::string& primary_key, const std::string& field, const std::vector<int64_t>& list);
    void set_subdoc(const std::string& primary_key, const std::string& field, const std::unordered_map<std::string, std::string>& sub);
    bool delete_one(const std::string& primary_key);
    std::vector<std::pair<std::string, Doc>> find_all() const;
    size_t count_documents() const;
};

class MongoDatabase {
    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<MongoCollection>> collections_;

public:
    MongoDatabase();
    std::shared_ptr<MongoCollection> collection(const std::string& name);
};

extern MongoDatabase mongodb;
void init_mongo();

} // namespace AnnieXMedia::core
