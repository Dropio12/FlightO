#include "trip.h"

#include <set>
#include <stdexcept>

#include "fare_provider.h"

namespace flighto {

namespace {

bool parse_nights(const std::string& s, int& out) {
    if (s.empty() || s.size() > 3) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

}  // namespace

CityStay parse_visit(const std::string& text) {
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        throw std::invalid_argument("visit '" + text + "' needs a stay length, like CDG:3-5 or CDG:4");
    }
    CityStay stay;
    stay.code = normalize_iata(text.substr(0, colon));
    if (stay.code.empty()) {
        throw std::invalid_argument("visit '" + text + "': '" + text.substr(0, colon) +
                                    "' is not a 3-letter airport code");
    }
    const std::string nights = text.substr(colon + 1);
    const auto dash = nights.find('-');
    bool ok;
    if (dash == std::string::npos) {
        ok = parse_nights(nights, stay.min_nights);
        stay.max_nights = stay.min_nights;
    } else {
        ok = parse_nights(nights.substr(0, dash), stay.min_nights) &&
             parse_nights(nights.substr(dash + 1), stay.max_nights);
    }
    if (!ok) {
        throw std::invalid_argument("visit '" + text + "': nights must look like 3-5 or 4");
    }
    if (stay.min_nights < 1) {
        throw std::invalid_argument("visit '" + text + "': stay at least 1 night");
    }
    if (stay.max_nights < stay.min_nights) {
        throw std::invalid_argument("visit '" + text + "': the most nights is smaller than the fewest");
    }
    return stay;
}

void validate(const TripRequest& r) {
    if (normalize_iata(r.home) != r.home || r.home.empty()) {
        throw std::invalid_argument("home airport '" + r.home + "' must be a 3-letter upper-case code");
    }
    if (r.cities.empty()) throw std::invalid_argument("add at least one city to visit");
    if (static_cast<int>(r.cities.size()) > kMaxCities) {
        throw std::invalid_argument("at most " + std::to_string(kMaxCities) + " cities per trip");
    }
    std::set<std::string> seen;
    long min_total = 0;
    for (const auto& c : r.cities) {
        if (normalize_iata(c.code) != c.code || c.code.empty()) {
            throw std::invalid_argument("city '" + c.code + "' must be a 3-letter upper-case code");
        }
        if (c.code == r.home) throw std::invalid_argument(c.code + " is the home airport, not a city to visit");
        if (!seen.insert(c.code).second) throw std::invalid_argument(c.code + " is listed twice");
        if (c.min_nights < 1) throw std::invalid_argument(c.code + ": stay at least 1 night");
        if (c.max_nights < c.min_nights) {
            throw std::invalid_argument(c.code + ": the most nights is smaller than the fewest");
        }
        min_total += c.min_nights;
    }
    if (r.latest_return < r.earliest_departure) {
        throw std::invalid_argument("the latest return date is before the earliest departure date");
    }
    const long window = r.latest_return - r.earliest_departure;
    if (window > kMaxWindowDays) {
        throw std::invalid_argument("the date window is " + std::to_string(window) + " days; the limit is " +
                                    std::to_string(kMaxWindowDays));
    }
    if (min_total > window) {
        throw std::invalid_argument("the shortest stays add up to " + std::to_string(min_total) +
                                    " nights, but " + r.earliest_departure.iso() + " to " +
                                    r.latest_return.iso() + " leaves only " + std::to_string(window));
    }
    if (r.alternatives < 0 || r.alternatives > kMaxAlternatives) {
        throw std::invalid_argument("alternatives must be between 0 and " + std::to_string(kMaxAlternatives));
    }
}

}  // namespace flighto
