// Fare provider that reads one-way fares from a CSV file.
//
// Format (one fare per line, prices in the trip currency):
//
//   # comment lines start with '#'
//   from,to,date,price
//   YUL,CDG,2027-05-01,512.00
//
// The header line is optional. If the same (from, to, date) appears twice,
// the cheaper price wins. A (from, to, date) that is not in the file means
// "no flight that day".
#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>

#include "fare_provider.h"

namespace flighto {

class CsvFareProvider : public FareProvider {
public:
    // Reads and validates the whole file. Throws FareError naming the line
    // number when a row is malformed, or when the file cannot be opened.
    explicit CsvFareProvider(const std::string& path);

    // Same, from text already in memory (`source_name` is used in messages).
    static CsvFareProvider from_string(const std::string& text, const std::string& source_name);

    std::optional<Cents> cheapest_fare(const std::string& from, const std::string& to,
                                       const Date& date) override;
    std::string describe() const override;

    std::size_t size() const { return fares_.size(); }

private:
    CsvFareProvider() = default;
    void load(std::istream& in, const std::string& source_name);
    static std::string key(const std::string& from, const std::string& to, const Date& date);

    std::string source_;
    std::unordered_map<std::string, Cents> fares_;
};

}  // namespace flighto
