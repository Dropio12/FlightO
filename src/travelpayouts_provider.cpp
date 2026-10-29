#include "travelpayouts_provider.h"

#include <cctype>

#include <nlohmann/json.hpp>

namespace flighto {

namespace {

constexpr std::size_t kMaxFailuresInARow = 5;
constexpr std::size_t kSaveEvery = 50;  // save the cache every N new answers

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool token_looks_valid(const std::string& t) {
    if (t.empty() || t.size() > 128) return false;
    for (char c : t) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool currency_looks_valid(const std::string& c) {
    if (c.size() != 3) return false;
    for (char ch : c) {
        if (!std::isalpha(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

}  // namespace

TravelpayoutsProvider::TravelpayoutsProvider(TravelpayoutsOptions options, std::unique_ptr<HttpClient> http)
    : options_(std::move(options)),
      http_(http ? std::move(http) : std::make_unique<CurlHttpClient>()),
      cache_(options_.cache_path, static_cast<std::int64_t>(options_.cache_ttl_hours * 3600.0)) {
    if (options_.token.empty()) {
        throw FareError(
            "the travelpayouts provider needs a token: set TRAVELPAYOUTS_TOKEN "
            "(free with a Travelpayouts account, under Profile > API token)");
    }
    if (!token_looks_valid(options_.token)) {
        throw FareError("TRAVELPAYOUTS_TOKEN should only contain letters and digits");
    }
    if (!currency_looks_valid(options_.currency)) {
        throw FareError("currency must be a 3-letter code like USD, got '" + options_.currency + "'");
    }
    options_.currency = upper(options_.currency);
}

TravelpayoutsProvider::~TravelpayoutsProvider() { cache_.save(); }

std::string TravelpayoutsProvider::endpoint_url(const std::string& from, const std::string& to, const Date& date,
                                                const std::string& currency) {
    return "https://api.travelpayouts.com/aviasales/v3/prices_for_dates?origin=" + from + "&destination=" + to +
           "&departure_at=" + date.iso() + "&one_way=true&direct=false&sorting=price&limit=10&currency=" +
           lower(currency);
}

std::optional<Cents> TravelpayoutsProvider::parse_response(const std::string& body, const Date& date,
                                                           const std::string& currency) {
    using nlohmann::json;
    const json doc = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        throw FareError("Travelpayouts sent something that is not a JSON object");
    }
    const bool success = doc.contains("success") && doc["success"].is_boolean() && doc["success"].get<bool>();
    if (!success) {
        std::string why = "no reason given";
        if (doc.contains("error") && doc["error"].is_string()) why = doc["error"].get<std::string>();
        throw FareError("Travelpayouts error: " + why);
    }
    if (doc.contains("currency") && doc["currency"].is_string() &&
        lower(doc["currency"].get<std::string>()) != lower(currency)) {
        throw FareError("asked Travelpayouts for " + upper(currency) + " prices but got " +
                        upper(doc["currency"].get<std::string>()));
    }
    if (!doc.contains("data") || !doc["data"].is_array()) return std::nullopt;

    const std::string day = date.iso();
    std::optional<Cents> best;
    for (const auto& ticket : doc["data"]) {
        if (!ticket.is_object() || !ticket.contains("departure_at") || !ticket["departure_at"].is_string() ||
            !ticket.contains("price") || !ticket["price"].is_number()) {
            continue;
        }
        // "2023-07-28T07:00:00+02:00": the first 10 characters are the local departure date.
        const std::string departs = ticket["departure_at"].get<std::string>();
        if (departs.compare(0, 10, day) != 0) continue;
        const double price = ticket["price"].get<double>();
        if (price < 0) continue;
        const Cents cents = to_cents(price);
        if (!best || cents < *best) best = cents;
    }
    return best;
}

std::optional<Cents> TravelpayoutsProvider::cheapest_fare(const std::string& from, const std::string& to,
                                                          const Date& date) {
    const std::string key = "travelpayouts|" + options_.currency + '|' + from + '|' + to + '|' + date.iso();
    if (const auto cached = cache_.get(key)) {
        ++cache_hits_;
        if (!cached->has_flight) return std::nullopt;
        return cached->price;
    }
    if (static_cast<long long>(requests_) >= options_.max_requests) {
        ++skipped_;
        return std::nullopt;
    }

    ++requests_;
    const HttpResponse response = http_->get(endpoint_url(from, to, date, options_.currency),
                                             {"Accept: application/json", "X-Access-Token: " + options_.token});
    if (response.status == 401 || response.status == 403) {
        throw FareError("Travelpayouts rejected the token (HTTP " + std::to_string(response.status) +
                        "). Check TRAVELPAYOUTS_TOKEN.");
    }

    std::string error;
    std::optional<Cents> price;
    if (response.status == 200) {
        try {
            price = parse_response(response.body, date, options_.currency);
        } catch (const FareError& e) {
            error = e.what();
        }
    } else if (response.status == 0) {
        error = "no answer: " + response.body.substr(0, 200);
    } else {
        error = "HTTP " + std::to_string(response.status);
    }

    if (!error.empty()) {
        ++failures_;
        last_error_ = from + " -> " + to + " on " + date.iso() + ": " + error;
        if (++failures_in_a_row_ >= kMaxFailuresInARow) {
            cache_.save();
            throw FareError(std::to_string(kMaxFailuresInARow) + " Travelpayouts requests failed in a row; last one: " +
                            last_error_);
        }
        return std::nullopt;  // not cached: try again next run
    }

    failures_in_a_row_ = 0;
    cache_.put(key, price ? CachedFare{true, *price} : CachedFare{false, 0});
    if (requests_ % kSaveEvery == 0) cache_.save();
    return price;
}

std::string TravelpayoutsProvider::describe() const {
    return "Travelpayouts Data API (prices_for_dates, " + options_.currency + ")";
}

std::vector<std::string> TravelpayoutsProvider::notes() const {
    std::vector<std::string> lines;
    std::string cache_line = "Travelpayouts: " + std::to_string(requests_) + " HTTP requests, " +
                             std::to_string(cache_hits_) + " answers from the disk cache";
    if (!cache_.path().empty()) cache_line += " (" + cache_.path() + ")";
    lines.push_back(cache_line);
    lines.push_back(
        "Live prices are cached fares Aviasales users found in the last 48 hours, not a booking quote. "
        "Check the price with the airline before you buy.");
    if (failures_ > 0) {
        lines.push_back("Warning: " + std::to_string(failures_) +
                        " lookups failed and were treated as no flight, so a cheaper trip may exist. Last error: " +
                        last_error_);
    }
    if (skipped_ > 0) {
        lines.push_back("Warning: stopped asking after " + std::to_string(options_.max_requests) + " requests; " +
                        std::to_string(skipped_) +
                        " fares were treated as no flight. Run again to continue from the cache, or raise "
                        "--max-requests.");
    }
    if (!cache_.warning().empty()) lines.push_back("Warning: " + cache_.warning());
    return lines;
}

}  // namespace flighto
