// A small JSON file that remembers live fare answers between runs, so
// repeating a search does not spend the provider's free quota again.
//
// "No flight that day" answers are remembered too. Entries older than the
// time-to-live are dropped when the file is loaded.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "money.h"

namespace flighto {

struct CachedFare {
    bool has_flight = false;
    Cents price = 0;  // meaningful only when has_flight
};

class DiskCache {
public:
    using Clock = std::function<std::int64_t()>;  // seconds since the Unix epoch

    // An empty `path` turns the cache into a no-op. A missing file is fine;
    // an unreadable or corrupt one is ignored and reported by warning().
    DiskCache(std::string path, std::int64_t ttl_seconds, Clock now = nullptr);

    std::optional<CachedFare> get(const std::string& key) const;
    void put(const std::string& key, const CachedFare& fare);

    // Writes the file (through a temporary file and a rename) if anything
    // changed. Returns false and sets warning() if it could not.
    bool save();

    std::size_t size() const { return items_.size(); }
    const std::string& path() const { return path_; }
    const std::string& warning() const { return warning_; }

private:
    struct Item {
        CachedFare fare;
        std::int64_t fetched_at = 0;
    };

    std::string path_;
    std::int64_t ttl_seconds_;
    Clock now_;
    std::map<std::string, Item> items_;
    bool dirty_ = false;
    std::string warning_;
};

}  // namespace flighto
