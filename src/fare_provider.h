// The one interface every fare source implements.
//
// The optimizer only ever asks one question: "what is the cheapest one-way
// fare from A to B on this date?". A provider answers with a price in cents,
// or std::nullopt when there is no flight (or no known fare) that day.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "date.h"
#include "money.h"

namespace flighto {

// Thrown when a provider cannot go on at all (unreadable file, rejected
// token, ...). A single missing fare is NOT an error: return std::nullopt.
class FareError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class FareProvider {
public:
    virtual ~FareProvider() = default;

    // `from` and `to` are upper-case IATA codes (3 letters).
    virtual std::optional<Cents> cheapest_fare(const std::string& from, const std::string& to,
                                               const Date& date) = 0;

    // One line naming the source, e.g. "csv file data/sample_fares.csv".
    virtual std::string describe() const = 0;

    // Extra lines for the end of the report (requests made, cache hits,
    // warnings). Empty by default.
    virtual std::vector<std::string> notes() const { return {}; }
};

// "cdg" -> "CDG". Returns an empty string when `code` is not 3 letters.
std::string normalize_iata(const std::string& code);

}  // namespace flighto
