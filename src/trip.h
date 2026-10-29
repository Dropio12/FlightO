// What the traveler asks for, and what the optimizer gives back.
#pragma once

#include <string>
#include <vector>

#include "date.h"
#include "money.h"

namespace flighto {

// A city to visit and how many nights to stay there (inclusive range).
struct CityStay {
    std::string code;  // IATA code, upper case
    int min_nights = 1;
    int max_nights = 1;
};

struct TripRequest {
    std::string home;               // where the trip starts (IATA code)
    std::vector<CityStay> cities;   // visited once each, in any order
    Date earliest_departure;        // first flight leaves on or after this date
    Date latest_return;             // every flight, and every stay, ends on or before this date
    bool return_home = true;        // false: the trip ends in the last city
    int alternatives = 2;           // how many runner-up itineraries to return
};

// Hard limits that keep memory small. The optimizer is exponential in the
// number of cities (see optimizer.h), so 10 is a practical ceiling.
constexpr int kMaxCities = 10;
constexpr int kMaxWindowDays = 366;
constexpr int kMaxAlternatives = 9;

// Parses "CDG:3-5" (3 to 5 nights) or "CDG:4" (exactly 4 nights).
// Throws std::invalid_argument with a readable message.
CityStay parse_visit(const std::string& text);

// Checks the request and throws std::invalid_argument explaining the first
// problem: bad codes, a city listed twice, the home airport listed as a
// city, a min stay above the max, dates in the wrong order, or stays that
// cannot fit in the window even at their minimum length.
void validate(const TripRequest& request);

// One flight of an itinerary.
struct Leg {
    std::string from;
    std::string to;
    Date date;
    Cents price = 0;
    int nights_after = 0;  // nights spent at `to` before the next flight (0 when `to` is home)
};

struct Itinerary {
    std::vector<Leg> legs;
    Cents total = 0;
    bool ends_at_home = true;
};

}  // namespace flighto
