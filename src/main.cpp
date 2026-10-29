// flighto: plan the cheapest multi-city trip from a set of one-way fares.
//
// Example (one line):
//   flighto --from YUL --visit CDG:3-5 --visit BCN:2-4 --visit FCO:3-4
//           --depart 2027-05-01 --return-by 2027-05-20 --fares data/sample_fares.csv

#include <cctype>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "csv_provider.h"
#include "optimizer.h"
#include "travelpayouts_provider.h"

using namespace flighto;

namespace {

const char kUsage[] = R"(Usage: flighto --from CODE --visit CODE:MIN-MAX [--visit ...] --depart DATE --return-by DATE [options]

Finds the cheapest order and dates to visit every city once, and shows the
next cheapest alternatives.

Required:
  --from CODE            home airport (3-letter IATA code) where the trip starts
  --visit CODE:MIN-MAX   a city to visit and how many nights to stay there.
                         Repeat it, or give a list: CDG:3-5,BCN:2-4.
                         CODE:N means exactly N nights.
  --depart DATE          earliest departure date, YYYY-MM-DD
  --return-by DATE       latest date of the flight home (with --no-return: the
                         date the last stay must end by)

Options:
  --no-return            end the trip in the last city instead of flying home
  --provider NAME        where fares come from: csv (default) or travelpayouts
  --fares FILE           fares file for the csv provider
                         (default: data/sample_fares.csv, which holds made-up SAMPLE prices)
  --currency CODE        currency of the prices (default: USD)
  --alternatives N       runner-up trips to show (default: 2, at most 9)
  --cache FILE           where live fares are cached (default: flighto_cache.json;
                         an empty value turns the cache off)
  --cache-hours H        how long a cached live fare is reused (default: 24)
  --max-requests N       most live API requests in one run (default: 600)
  -h, --help             show this help

Environment:
  TRAVELPAYOUTS_TOKEN    API token for --provider travelpayouts (free account)

Exit status: 0 a trip was found, 1 bad input or provider error, 2 no trip fits.
)";

struct Options {
    TripRequest trip;
    std::string provider = "csv";
    std::string fares_file = "data/sample_fares.csv";
    std::string currency = "USD";
    std::string cache_file = "flighto_cache.json";
    double cache_hours = 24;
    int max_requests = 600;
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

int parse_int(const std::string& flag, const std::string& value) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(value, &used);
        if (used == value.size()) return v;
    } catch (const std::exception&) {
    }
    throw UsageError(flag + " needs a whole number, got '" + value + "'");
}

double parse_double(const std::string& flag, const std::string& value) {
    try {
        std::size_t used = 0;
        const double v = std::stod(value, &used);
        if (used == value.size() && v >= 0) return v;
    } catch (const std::exception&) {
    }
    throw UsageError(flag + " needs a number >= 0, got '" + value + "'");
}

Options parse_args(int argc, char** argv) {
    Options o;
    bool have_from = false, have_depart = false, have_return = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw UsageError(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            std::exit(0);
        } else if (arg == "--from") {
            o.trip.home = normalize_iata(value());
            if (o.trip.home.empty()) throw UsageError("--from needs a 3-letter airport code");
            have_from = true;
        } else if (arg == "--visit") {
            std::stringstream list(value());
            std::string item;
            while (std::getline(list, item, ',')) {
                if (!item.empty()) o.trip.cities.push_back(parse_visit(item));
            }
        } else if (arg == "--depart") {
            o.trip.earliest_departure = Date::parse(value());
            have_depart = true;
        } else if (arg == "--return-by") {
            o.trip.latest_return = Date::parse(value());
            have_return = true;
        } else if (arg == "--no-return") {
            o.trip.return_home = false;
        } else if (arg == "--provider") {
            o.provider = value();
            if (o.provider != "csv" && o.provider != "travelpayouts") {
                throw UsageError("--provider must be csv or travelpayouts, got '" + o.provider + "'");
            }
        } else if (arg == "--fares") {
            o.fares_file = value();
        } else if (arg == "--currency") {
            o.currency = value();
            for (char& c : o.currency) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else if (arg == "--alternatives") {
            o.trip.alternatives = parse_int(arg, value());
        } else if (arg == "--cache") {
            o.cache_file = value();
        } else if (arg == "--cache-hours") {
            o.cache_hours = parse_double(arg, value());
        } else if (arg == "--max-requests") {
            o.max_requests = parse_int(arg, value());
            if (o.max_requests < 0) throw UsageError("--max-requests cannot be negative");
        } else {
            throw UsageError("unknown option '" + arg + "' (see --help)");
        }
    }
    if (!have_from) throw UsageError("missing --from (your home airport)");
    if (o.trip.cities.empty()) throw UsageError("missing --visit (at least one city)");
    if (!have_depart) throw UsageError("missing --depart (earliest departure date)");
    if (!have_return) throw UsageError("missing --return-by (latest return date)");
    return o;
}

std::string pad_left(const std::string& s, std::size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

std::string nights_text(int n) { return std::to_string(n) + (n == 1 ? " night" : " nights"); }

void print_trip(const Itinerary& trip) {
    for (std::size_t i = 0; i < trip.legs.size(); ++i) {
        const Leg& leg = trip.legs[i];
        const bool last = i + 1 == trip.legs.size();
        std::string then;
        if (last && trip.ends_at_home) {
            then = "home";
        } else if (leg.nights_after > 0) {
            then = "then " + nights_text(leg.nights_after) + " in " + leg.to;
        } else {
            then = "trip ends in " + leg.to;
        }
        std::cout << "  " << leg.date.weekday_name() << ' ' << leg.date.iso() << "  " << leg.from << " -> " << leg.to
                  << "  " << pad_left(format_money(leg.price), 9) << "  " << then << '\n';
    }
    std::string order = trip.legs.front().from;
    for (const Leg& leg : trip.legs) order += " > " + leg.to;
    std::cout << "  Order: " << order << '\n';
}

int run(const Options& o) {
    validate(o.trip);  // report a bad request before loading fares or printing anything

    std::unique_ptr<FareProvider> provider;
    TravelpayoutsProvider* live = nullptr;
    if (o.provider == "csv") {
        provider = std::make_unique<CsvFareProvider>(o.fares_file);
    } else {
        TravelpayoutsOptions t;
        if (const char* token = std::getenv("TRAVELPAYOUTS_TOKEN")) t.token = token;
        t.currency = o.currency;
        t.cache_path = o.cache_file;
        t.cache_ttl_hours = o.cache_hours;
        t.max_requests = o.max_requests;
        auto p = std::make_unique<TravelpayoutsProvider>(t);
        live = p.get();
        provider = std::move(p);
    }

    const TripRequest& trip = o.trip;
    std::cout << "FlightO: cheapest trip from " << trip.home << '\n';
    std::cout << "  Visit:   ";
    for (std::size_t i = 0; i < trip.cities.size(); ++i) {
        const auto& c = trip.cities[i];
        std::cout << (i ? ", " : "") << c.code << ' '
                  << (c.min_nights == c.max_nights ? nights_text(c.min_nights)
                                                   : std::to_string(c.min_nights) + "-" + nights_text(c.max_nights));
    }
    std::cout << (trip.cities.size() > 1 ? " (any order)\n" : "\n");
    std::cout << "  Window:  " << trip.earliest_departure.iso() << " to " << trip.latest_return.iso()
              << (trip.return_home ? ", back in " + trip.home + " by the end" : ", no return flight") << '\n';
    std::cout << "  Fares:   " << provider->describe() << ", prices in " << o.currency << "\n\n";

    const SearchResult result = find_cheapest_trips(trip, *provider);
    if (live) live->save_cache();

    if (result.itineraries.empty()) {
        std::cout << "No trip fits: with these fares there is no way to visit every city inside the window.\n"
                     "Try a wider window, more flexible stays, or another fares source.\n";
    }
    for (std::size_t k = 0; k < result.itineraries.size(); ++k) {
        const Itinerary& it = result.itineraries[k];
        if (k == 0) {
            std::cout << "Best trip: " << format_money(it.total) << ' ' << o.currency << '\n';
        } else {
            std::cout << "\nAlternative " << k << ": " << format_money(it.total) << ' ' << o.currency << " (+"
                      << format_money(it.total - result.itineraries[0].total) << ")\n";
        }
        print_trip(it);
    }

    const SearchStats& s = result.stats;
    std::cout << "\nFares looked up: " << s.fare_lookups << " distinct (from, to, date), " << s.missing_fares
              << " with no flight\n";
    std::ostringstream ms;
    ms.setf(std::ios::fixed);
    ms.precision(1);
    ms << s.millis;
    std::cout << "Search: " << s.states_reached << " of " << s.states_total << " states reached in " << ms.str()
              << " ms\n";
    for (const auto& line : provider->notes()) std::cout << line << '\n';
    return result.itineraries.empty() ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const UsageError& e) {
        std::cerr << "flighto: " << e.what() << "\n\n" << kUsage;
        return 1;
    } catch (const std::invalid_argument& e) {
        std::cerr << "flighto: " << e.what() << '\n';
        return 1;
    } catch (const FareError& e) {
        std::cerr << "flighto: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "flighto: unexpected error: " << e.what() << '\n';
        return 1;
    }
}
