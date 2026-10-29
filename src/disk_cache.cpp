#include "disk_cache.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace flighto {

namespace {

constexpr const char* kFormat = "flighto-fare-cache-v1";

std::int64_t system_now() { return static_cast<std::int64_t>(std::time(nullptr)); }

}  // namespace

DiskCache::DiskCache(std::string path, std::int64_t ttl_seconds, Clock now)
    : path_(std::move(path)), ttl_seconds_(ttl_seconds), now_(now ? std::move(now) : Clock(system_now)) {
    if (path_.empty()) return;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;  // first run: no cache yet

    const auto doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    const bool valid = !doc.is_discarded() && doc.is_object() && doc.contains("format") &&
                       doc["format"].is_string() && doc["format"].get<std::string>() == kFormat &&
                       doc.contains("fares") && doc["fares"].is_object();
    if (!valid) {
        warning_ = "ignored unreadable fare cache " + path_ + " (it will be rewritten)";
        dirty_ = true;
        return;
    }
    const std::int64_t now_s = now_();
    for (const auto& [key, value] : doc["fares"].items()) {
        if (!value.is_object() || !value.contains("fetched_at") || !value["fetched_at"].is_number_integer()) {
            continue;
        }
        Item item;
        item.fetched_at = value["fetched_at"].get<std::int64_t>();
        if (now_s - item.fetched_at > ttl_seconds_) {
            dirty_ = true;  // expired: drop it
            continue;
        }
        const auto& price = value.contains("price_cents") ? value["price_cents"] : nlohmann::json();
        if (price.is_number_integer()) {
            item.fare = CachedFare{true, price.get<Cents>()};
        } else if (price.is_null()) {
            item.fare = CachedFare{false, 0};
        } else {
            continue;
        }
        items_[key] = item;
    }
}

std::optional<CachedFare> DiskCache::get(const std::string& key) const {
    const auto it = items_.find(key);
    if (it == items_.end()) return std::nullopt;
    return it->second.fare;
}

void DiskCache::put(const std::string& key, const CachedFare& fare) {
    items_[key] = Item{fare, now_()};
    dirty_ = true;
}

bool DiskCache::save() {
    if (path_.empty() || !dirty_) return true;
    nlohmann::json fares = nlohmann::json::object();
    for (const auto& [key, item] : items_) {
        fares[key] = {
            {"price_cents", item.fare.has_flight ? nlohmann::json(item.fare.price) : nlohmann::json(nullptr)},
            {"fetched_at", item.fetched_at},
        };
    }
    const nlohmann::json doc = {{"format", kFormat}, {"fares", fares}};

    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << doc.dump(1) << '\n';
        if (!out) {
            warning_ = "could not write fare cache " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);  // replaces the old file
    if (ec) {
        warning_ = "could not save fare cache " + path_ + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    dirty_ = false;
    return true;
}

}  // namespace flighto
