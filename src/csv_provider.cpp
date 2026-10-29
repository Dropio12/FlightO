#include "csv_provider.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace flighto {

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split_commas(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    return fields;
}

bool parse_price(const std::string& text, double& out) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return errno == 0 && end == text.c_str() + text.size() && out >= 0.0 && std::isfinite(out);
}

}  // namespace

CsvFareProvider::CsvFareProvider(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw FareError("cannot open fares file '" + path + "'");
    load(in, path);
}

CsvFareProvider CsvFareProvider::from_string(const std::string& text, const std::string& source_name) {
    CsvFareProvider p;
    std::istringstream in(text);
    p.load(in, source_name);
    return p;
}

void CsvFareProvider::load(std::istream& in, const std::string& source_name) {
    source_ = source_name;
    std::string raw;
    int line_no = 0;
    bool seen_data_or_header = false;
    while (std::getline(in, raw)) {
        ++line_no;
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;

        const auto f = split_commas(line);
        auto fail = [&](const std::string& why) {
            throw FareError(source_name + " line " + std::to_string(line_no) + ": " + why + " (got '" +
                            line + "')");
        };

        if (!seen_data_or_header) {
            seen_data_or_header = true;
            if (!f.empty() && (f[0] == "from" || f[0] == "FROM" || f[0] == "From")) continue;  // header
        }
        if (f.size() != 4) fail("expected 4 fields: from,to,date,price");

        const std::string from = normalize_iata(f[0]);
        const std::string to = normalize_iata(f[1]);
        if (from.empty()) fail("'from' must be a 3-letter airport code");
        if (to.empty()) fail("'to' must be a 3-letter airport code");
        if (from == to) fail("'from' and 'to' are the same airport");
        const auto date = Date::try_parse(f[2]);
        if (!date) fail("date must look like 2027-05-01");
        double price = 0;
        if (!parse_price(f[3], price)) fail("price must be a number >= 0");

        const Cents cents = to_cents(price);
        auto [it, inserted] = fares_.emplace(key(from, to, *date), cents);
        if (!inserted && cents < it->second) it->second = cents;
    }
}

std::string CsvFareProvider::key(const std::string& from, const std::string& to, const Date& date) {
    return from + '|' + to + '|' + date.iso();
}

std::optional<Cents> CsvFareProvider::cheapest_fare(const std::string& from, const std::string& to,
                                                    const Date& date) {
    const auto it = fares_.find(key(from, to, date));
    if (it == fares_.end()) return std::nullopt;
    return it->second;
}

std::string CsvFareProvider::describe() const {
    return "csv file " + source_ + " (" + std::to_string(fares_.size()) + " fares)";
}

}  // namespace flighto
