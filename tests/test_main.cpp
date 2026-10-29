// Unit tests for FlightO. Run with `ctest` or the flighto_tests executable.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "csv_provider.h"
#include "date.h"
#include "disk_cache.h"
#include "optimizer.h"
#include "travelpayouts_provider.h"
#include "trip.h"

using namespace flighto;

namespace {

const std::string kFixtures = FLIGHTO_FIXTURES_DIR;

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Date d(const char* iso) { return Date::parse(iso); }

TripRequest tiny_request() {
    TripRequest r;
    r.home = "HOM";
    r.cities = {{"AAA", 1, 2}, {"BBB", 1, 1}};
    r.earliest_departure = d("2027-01-01");
    r.latest_return = d("2027-01-05");
    r.alternatives = 2;
    return r;
}

std::vector<Cents> totals(const SearchResult& result) {
    std::vector<Cents> out;
    for (const auto& it : result.itineraries) out.push_back(it.total);
    return out;
}

// Wraps a provider and counts how often each (from, to, date) is asked.
class CountingProvider : public FareProvider {
public:
    explicit CountingProvider(FareProvider& inner) : inner_(inner) {}
    std::optional<Cents> cheapest_fare(const std::string& from, const std::string& to, const Date& date) override {
        ++calls[from + '|' + to + '|' + date.iso()];
        return inner_.cheapest_fare(from, to, date);
    }
    std::string describe() const override { return "counting"; }
    std::map<std::string, int> calls;

private:
    FareProvider& inner_;
};

// Deterministic pseudo-random fares: about a quarter of the days have no
// flight, the rest cost between 50.00 and 499.00.
class RandomProvider : public FareProvider {
public:
    explicit RandomProvider(std::uint32_t seed) : seed_(seed) {}
    std::optional<Cents> cheapest_fare(const std::string& from, const std::string& to, const Date& date) override {
        std::uint32_t h = seed_ ^ 2166136261u;
        for (char c : from + to + date.iso()) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        if (h % 4 == 0) return std::nullopt;
        return static_cast<Cents>(50 + (h >> 8) % 450) * 100;
    }
    std::string describe() const override { return "random"; }

private:
    std::uint32_t seed_;
};

// Tries every order, start day and stay length. Returns all trip totals, sorted.
std::vector<Cents> brute_force_totals(const TripRequest& r, FareProvider& fares) {
    const int n = static_cast<int>(r.cities.size());
    const int W = static_cast<int>(r.latest_return - r.earliest_departure);
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::vector<Cents> all;
    auto fare = [&](const std::string& a, const std::string& b, int day) {
        return fares.cheapest_fare(a, b, r.earliest_departure.plus_days(day));
    };
    std::function<void(int, int, Cents)> go = [&](int pos, int day, Cents cost) {
        // Just landed in order[pos] on `day`.
        const CityStay& here = r.cities[order[pos]];
        if (pos == n - 1) {
            if (!r.return_home) {
                if (day + here.min_nights <= W) all.push_back(cost);
                return;
            }
            for (int s = here.min_nights; s <= here.max_nights && day + s <= W; ++s) {
                if (auto f = fare(here.code, r.home, day + s)) all.push_back(cost + *f);
            }
            return;
        }
        for (int s = here.min_nights; s <= here.max_nights && day + s <= W; ++s) {
            if (auto f = fare(here.code, r.cities[order[pos + 1]].code, day + s)) go(pos + 1, day + s, cost + *f);
        }
    };
    do {
        for (int t = 0; t <= W; ++t) {
            if (auto f = fare(r.home, r.cities[order[0]].code, t)) go(0, t, *f);
        }
    } while (std::next_permutation(order.begin(), order.end()));
    std::sort(all.begin(), all.end());
    return all;
}

// A fake HTTP client that answers from a list and records requests.
class FakeHttp : public HttpClient {
public:
    struct Call {
        std::string url;
        std::vector<std::string> headers;
    };
    explicit FakeHttp(std::vector<Call>* calls, std::function<HttpResponse(const std::string&)> answer)
        : calls_(calls), answer_(std::move(answer)) {}
    HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override {
        calls_->push_back({url, headers});
        return answer_(url);
    }

private:
    std::vector<Call>* calls_;
    std::function<HttpResponse(const std::string&)> answer_;
};

std::string temp_path(const std::string& name) {
    const auto p = std::filesystem::temp_directory_path() / ("flighto_test_" + name);
    std::filesystem::remove(p);
    return p.string();
}

}  // namespace

// ---------------------------------------------------------------- dates

TEST_CASE("dates parse strictly and print back the same") {
    CHECK(d("2027-05-01").iso() == "2027-05-01");
    CHECK(d("2024-02-29").iso() == "2024-02-29");  // leap year
    CHECK_FALSE(Date::try_parse("2027-02-29"));    // not a leap year
    CHECK_FALSE(Date::try_parse("1900-02-29"));    // century, not a leap year
    CHECK(Date::try_parse("2000-02-29"));          // divisible by 400, leap year
    CHECK_FALSE(Date::try_parse("2027-04-31"));
    CHECK_FALSE(Date::try_parse("2027-13-01"));
    CHECK_FALSE(Date::try_parse("2027-00-10"));
    CHECK_FALSE(Date::try_parse("2027-5-01"));
    CHECK_FALSE(Date::try_parse("2027/05/01"));
    CHECK_FALSE(Date::try_parse("tomorrow"));
    CHECK_FALSE(Date::try_parse(""));
    CHECK_THROWS_AS(Date::parse("2027-02-30"), std::invalid_argument);
    CHECK_THROWS_AS(Date::from_ymd(2027, 2, 29), std::invalid_argument);
    CHECK(Date::from_ymd(2027, 5, 1) == d("2027-05-01"));
}

TEST_CASE("day math crosses months, years and leap days") {
    CHECK(d("1970-01-01").days_since_epoch() == 0);
    CHECK(d("2024-03-01") - d("2024-02-28") == 2);
    CHECK(d("2027-03-01") - d("2027-02-28") == 1);
    CHECK(d("2027-01-01") - d("2026-12-31") == 1);
    CHECK(d("2028-01-01") - d("2027-01-01") == 365);
    CHECK(d("2029-01-01") - d("2028-01-01") == 366);
    CHECK(d("2027-05-01").plus_days(30) == d("2027-05-31"));
    CHECK(d("2027-05-01").plus_days(31) == d("2027-06-01"));
    CHECK(d("2027-03-01").plus_days(-1) == d("2027-02-28"));
    CHECK(d("2027-05-01").year() == 2027);
    CHECK(d("2027-05-01").month() == 5);
    CHECK(d("2027-05-01").day() == 1);
    CHECK(std::string(d("1970-01-01").weekday_name()) == "Thu");
    CHECK(std::string(d("2000-01-01").weekday_name()) == "Sat");
    CHECK(std::string(d("1969-12-31").weekday_name()) == "Wed");
    CHECK(d("2027-05-01") < d("2027-05-02"));
}

// ---------------------------------------------------------------- stays and requests

TEST_CASE("visits parse into stay windows") {
    const CityStay a = parse_visit("CDG:3-5");
    CHECK(a.code == "CDG");
    CHECK(a.min_nights == 3);
    CHECK(a.max_nights == 5);
    const CityStay b = parse_visit("fco:4");
    CHECK(b.code == "FCO");
    CHECK(b.min_nights == 4);
    CHECK(b.max_nights == 4);
    CHECK_THROWS_AS(parse_visit("CDG"), std::invalid_argument);
    CHECK_THROWS_AS(parse_visit("CDG:0-2"), std::invalid_argument);
    CHECK_THROWS_AS(parse_visit("CDG:5-3"), std::invalid_argument);
    CHECK_THROWS_AS(parse_visit("CD:1-2"), std::invalid_argument);
    CHECK_THROWS_AS(parse_visit("CDG:a-b"), std::invalid_argument);
    CHECK_THROWS_AS(parse_visit("CDG:3-"), std::invalid_argument);
}

TEST_CASE("requests that cannot work are refused with a reason") {
    TripRequest r = tiny_request();
    CHECK_NOTHROW(validate(r));

    SUBCASE("a city listed twice") {
        r.cities.push_back({"AAA", 1, 1});
        CHECK_THROWS_WITH_AS(validate(r), "AAA is listed twice", std::invalid_argument);
    }
    SUBCASE("home listed as a city") {
        r.cities.push_back({"HOM", 1, 1});
        CHECK_THROWS_AS(validate(r), std::invalid_argument);
    }
    SUBCASE("dates in the wrong order") {
        r.latest_return = d("2026-12-31");
        CHECK_THROWS_AS(validate(r), std::invalid_argument);
    }
    SUBCASE("minimum stays longer than the window") {
        r.cities = {{"AAA", 3, 3}, {"BBB", 2, 2}};  // 5 nights, window has 4
        CHECK_THROWS_AS(validate(r), std::invalid_argument);
    }
    SUBCASE("too many cities") {
        r.cities.clear();
        const char* codes[] = {"AAA", "BBB", "CCC", "DDD", "EEE", "FFF", "GGG", "HHH", "III", "JJJ", "KKK"};
        for (const char* c : codes) r.cities.push_back({c, 1, 1});
        r.latest_return = d("2027-03-01");
        CHECK_THROWS_AS(validate(r), std::invalid_argument);
    }
}

// ---------------------------------------------------------------- CSV provider

TEST_CASE("CSV fares: comments, header, case, duplicates and missing days") {
    auto p = CsvFareProvider::from_string(
        "# SAMPLE\n"
        "from,to,date,price\n"
        "\n"
        "yul,cdg,2027-05-01,512.50\r\n"
        "YUL,CDG,2027-05-01,499\n"  // same flight listed twice: the cheaper one wins
        "CDG,YUL,2027-05-09,600\n",
        "inline");
    CHECK(p.size() == 2);
    CHECK(p.cheapest_fare("YUL", "CDG", d("2027-05-01")) == Cents{49900});
    CHECK(p.cheapest_fare("CDG", "YUL", d("2027-05-09")) == Cents{60000});
    CHECK_FALSE(p.cheapest_fare("YUL", "CDG", d("2027-05-02")));  // no flight that day
    CHECK_FALSE(p.cheapest_fare("CDG", "YUL", d("2027-05-01")));
}

TEST_CASE("CSV fares: a bad row names its line number") {
    CHECK_THROWS_WITH_AS(CsvFareProvider::from_string("from,to,date,price\nYUL,CDG,2027-05-01,512\nYUL,CDG,May 2,1\n",
                                                      "bad.csv"),
                         doctest::Contains("bad.csv line 3"), FareError);
    CHECK_THROWS_AS(CsvFareProvider::from_string("YUL,CDG,2027-05-01,-4\n", "x"), FareError);
    CHECK_THROWS_AS(CsvFareProvider::from_string("YUL,CDG,2027-05-01\n", "x"), FareError);
    CHECK_THROWS_AS(CsvFareProvider("does/not/exist.csv"), FareError);
}

TEST_CASE("the shipped sample fares load") {
    CsvFareProvider p(kFixtures + "/../../data/sample_fares.csv");
    CHECK(p.size() > 500);
}

// ---------------------------------------------------------------- optimizer, hand-checked
//
// tests/fixtures/tiny_fares.csv, worked out by hand. Days are counted from
// 2027-01-01 (day 0) to 2027-01-05 (day 4). AAA allows 1-2 nights, BBB
// exactly 1. "-" means no flight that day.
//
// Order HOM > AAA > BBB > HOM
//   leave day 0 (100): AAA 1 night -> AAA>BBB day 1 (40), BBB>HOM day 2 (-)  no trip
//                      AAA 2 nights -> AAA>BBB day 2 (30), BBB>HOM day 3 (60) = 190
//   leave day 1 (80):  AAA 1 night -> AAA>BBB day 2 (30), BBB>HOM day 3 (60) = 170
//                      AAA 2 nights -> AAA>BBB day 3 (20), BBB>HOM day 4 (40) = 140
//   leave day 2 (60):  AAA 1 night -> AAA>BBB day 3 (20), BBB>HOM day 4 (40) = 120  <- best
//                      AAA 2 nights -> AAA>BBB day 4 (-)                       no trip
//   leave day 3: HOM>AAA (-)
// Order HOM > BBB > AAA > HOM
//   leave day 0 (90):  BBB>AAA day 1 (25), AAA 1 night -> AAA>HOM day 2 (-)    no trip
//                                          AAA 2 nights -> AAA>HOM day 3 (70)  = 185
//   leave day 1 (50):  BBB>AAA day 2 (35), AAA 1 night -> AAA>HOM day 3 (70)   = 155
//                                          AAA 2 nights -> AAA>HOM day 4 (120) = 205
//   leave day 2: HOM>BBB (-)
// Sorted: 120, 140, 155, 170, 185, 190, 205.

TEST_CASE("optimizer finds the hand-checked cheapest trip and the next two") {
    CsvFareProvider fares(kFixtures + "/tiny_fares.csv");
    const SearchResult res = find_cheapest_trips(tiny_request(), fares);
    REQUIRE(res.itineraries.size() == 3);
    CHECK(totals(res) == std::vector<Cents>{12000, 14000, 15500});

    const Itinerary& best = res.itineraries[0];
    REQUIRE(best.legs.size() == 3);
    CHECK(best.legs[0].from == "HOM");
    CHECK(best.legs[0].to == "AAA");
    CHECK(best.legs[0].date == d("2027-01-03"));
    CHECK(best.legs[0].price == 6000);
    CHECK(best.legs[0].nights_after == 1);
    CHECK(best.legs[1].from == "AAA");
    CHECK(best.legs[1].to == "BBB");
    CHECK(best.legs[1].date == d("2027-01-04"));
    CHECK(best.legs[1].price == 2000);
    CHECK(best.legs[1].nights_after == 1);
    CHECK(best.legs[2].from == "BBB");
    CHECK(best.legs[2].to == "HOM");
    CHECK(best.legs[2].date == d("2027-01-05"));
    CHECK(best.legs[2].price == 4000);

    const Itinerary& second = res.itineraries[1];  // same order, leave a day earlier, 2 nights in AAA
    CHECK(second.legs[0].date == d("2027-01-02"));
    CHECK(second.legs[0].nights_after == 2);

    const Itinerary& third = res.itineraries[2];  // the other order
    CHECK(third.legs[0].to == "BBB");
    CHECK(third.legs[1].to == "AAA");
    CHECK(third.legs[2].date == d("2027-01-04"));

    for (const auto& it : res.itineraries) {
        Cents sum = 0;
        for (const auto& leg : it.legs) sum += leg.price;
        CHECK(sum == it.total);
    }
}

TEST_CASE("optimizer respects the stay windows") {
    CsvFareProvider fares(kFixtures + "/tiny_fares.csv");
    TripRequest r = tiny_request();
    r.cities[0].max_nights = 1;  // AAA exactly 1 night: 140, 185, 190 and 205 are gone
    CHECK(totals(find_cheapest_trips(r, fares)) == std::vector<Cents>{12000, 15500, 17000});

    r.cities[0] = {"AAA", 2, 2};  // AAA exactly 2 nights
    CHECK(totals(find_cheapest_trips(r, fares)) == std::vector<Cents>{14000, 18500, 19000});
}

TEST_CASE("optimizer respects the latest return date") {
    CsvFareProvider fares(kFixtures + "/tiny_fares.csv");
    TripRequest r = tiny_request();
    r.latest_return = d("2027-01-04");  // trips flying home on 2027-01-05 no longer count
    CHECK(totals(find_cheapest_trips(r, fares)) == std::vector<Cents>{15500, 17000, 18500});

    r.earliest_departure = d("2027-01-02");  // and none may leave on 2027-01-01
    CHECK(totals(find_cheapest_trips(r, fares)) == std::vector<Cents>{15500, 17000});
}

TEST_CASE("optimizer without a return flight") {
    // Ends in the last city, whose minimum stay must still fit in the window:
    // HOM>AAA>BBB: 140, 130, 110, 100, 80.  HOM>BBB>AAA: 115, 85.
    CsvFareProvider fares(kFixtures + "/tiny_fares.csv");
    TripRequest r = tiny_request();
    r.return_home = false;
    const SearchResult res = find_cheapest_trips(r, fares);
    CHECK(totals(res) == std::vector<Cents>{8000, 8500, 10000});
    REQUIRE(res.itineraries[0].legs.size() == 2);
    CHECK(res.itineraries[0].legs.back().to == "BBB");
    CHECK_FALSE(res.itineraries[0].ends_at_home);
}

TEST_CASE("optimizer handles missing fares") {
    SUBCASE("no fares at all: no trip, no crash") {
        auto empty = CsvFareProvider::from_string("from,to,date,price\n", "empty");
        const SearchResult res = find_cheapest_trips(tiny_request(), empty);
        CHECK(res.itineraries.empty());
        CHECK(res.stats.fare_lookups > 0);
        CHECK(res.stats.missing_fares == res.stats.fare_lookups);
    }
    SUBCASE("fewer trips exist than alternatives asked") {
        auto two = CsvFareProvider::from_string(
            "HOM,AAA,2027-01-01,100\nAAA,BBB,2027-01-02,10\nBBB,HOM,2027-01-03,10\nBBB,HOM,2027-01-04,20\n", "two");
        TripRequest r = tiny_request();
        r.cities[1].max_nights = 2;  // BBB 1-2 nights, so both flights home count: 100+10+10 and 100+10+20
        CHECK(totals(find_cheapest_trips(r, two)) == std::vector<Cents>{12000, 13000});
    }
    SUBCASE("the only way home is missing") {
        auto stuck = CsvFareProvider::from_string("HOM,AAA,2027-01-01,100\nAAA,BBB,2027-01-02,10\n", "stuck");
        CHECK(find_cheapest_trips(tiny_request(), stuck).itineraries.empty());
    }
}

TEST_CASE("each (from, to, date) is asked of the provider at most once") {
    RandomProvider random(7);
    CountingProvider counting(random);
    TripRequest r;
    r.home = "YUL";
    r.cities = {{"CDG", 2, 4}, {"BCN", 1, 3}, {"FCO", 2, 3}, {"LIS", 1, 2}};
    r.earliest_departure = d("2027-05-01");
    r.latest_return = d("2027-05-25");
    const SearchResult res = find_cheapest_trips(r, counting);
    CHECK(res.stats.fare_lookups == counting.calls.size());
    for (const auto& [key, count] : counting.calls) {
        CHECK_MESSAGE(count == 1, key);
    }
    // Never more than (n + 1) * n * (days in the window) distinct lookups.
    CHECK(res.stats.fare_lookups <= 5u * 4u * 25u);
}

TEST_CASE("optimizer matches brute force on random small trips") {
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        RandomProvider fares(seed);
        TripRequest r;
        r.home = "HOM";
        r.cities = {{"AAA", 1, 3}, {"BBB", 2, 2}, {"CCC", 1, 2}};
        if (seed % 3 == 0) r.cities.push_back({"DDD", 1, 2});
        r.earliest_departure = d("2027-02-20");
        r.latest_return = d("2027-03-05");  // crosses the end of February
        r.return_home = seed % 4 != 1;
        r.alternatives = 4;

        std::vector<Cents> expected = brute_force_totals(r, fares);
        if (expected.size() > 5) expected.resize(5);
        CAPTURE(seed);
        CHECK(totals(find_cheapest_trips(r, fares)) == expected);
    }
}

TEST_CASE("a large request stays fast and small") {
    // 8 cities over a 90-day window, the size the README promises.
    RandomProvider fares(99);
    TripRequest r;
    r.home = "YUL";
    const char* codes[] = {"CDG", "BCN", "FCO", "LIS", "AMS", "BER", "MAD", "ATH"};
    for (const char* c : codes) r.cities.push_back({c, 2, 5});
    r.earliest_departure = d("2027-05-01");
    r.latest_return = r.earliest_departure.plus_days(90);
    const SearchResult res = find_cheapest_trips(r, fares);
    REQUIRE_FALSE(res.itineraries.empty());
    CHECK(res.itineraries[0].legs.size() == 9);
    CHECK(res.stats.fare_lookups <= 9u * 8u * 91u);
    MESSAGE("8 cities, 90 days: " << res.stats.millis << " ms, " << res.stats.fare_lookups << " fare lookups");
}

// ---------------------------------------------------------------- Travelpayouts provider

TEST_CASE("Travelpayouts: parse the saved sample response") {
    const std::string body = read_file(kFixtures + "/travelpayouts_prices_for_dates.json");
    REQUIRE_FALSE(body.empty());
    CHECK(TravelpayoutsProvider::parse_response(body, d("2023-07-28"), "USD") == Cents{592900});
    CHECK_FALSE(TravelpayoutsProvider::parse_response(body, d("2023-07-29"), "USD"));  // no ticket that day
    CHECK_THROWS_AS(TravelpayoutsProvider::parse_response(body, d("2023-07-28"), "EUR"), FareError);
}

TEST_CASE("Travelpayouts: other response shapes") {
    const Date day = d("2027-05-03");
    CHECK_FALSE(TravelpayoutsProvider::parse_response(R"({"success":true,"data":[],"currency":"usd"})", day, "usd"));
    CHECK_FALSE(TravelpayoutsProvider::parse_response(R"({"success":true,"data":{},"error":null})", day, "usd"));
    CHECK(TravelpayoutsProvider::parse_response(
              R"({"success":true,"currency":"usd","data":[
                    {"price":410,"departure_at":"2027-05-03T18:40:00-04:00"},
                    {"price":389.5,"departure_at":"2027-05-03T09:05:00-04:00"},
                    {"price":120,"departure_at":"2027-05-04T09:05:00-04:00"},
                    {"price":"oops","departure_at":"2027-05-03T09:05:00-04:00"}]})",
              day, "USD") == Cents{38950});
    CHECK_THROWS_WITH_AS(
        TravelpayoutsProvider::parse_response(R"({"success":false,"data":{},"error":"bad origin"})", day, "usd"),
        "Travelpayouts error: bad origin", FareError);
    CHECK_THROWS_AS(TravelpayoutsProvider::parse_response("<html>502</html>", day, "usd"), FareError);
}

TEST_CASE("Travelpayouts: URL has the query but never the token") {
    const std::string url = TravelpayoutsProvider::endpoint_url("YUL", "CDG", d("2027-05-01"), "USD");
    CHECK(url.rfind("https://api.travelpayouts.com/aviasales/v3/prices_for_dates?", 0) == 0);
    CHECK(url.find("origin=YUL") != std::string::npos);
    CHECK(url.find("destination=CDG") != std::string::npos);
    CHECK(url.find("departure_at=2027-05-01") != std::string::npos);
    CHECK(url.find("one_way=true") != std::string::npos);
    CHECK(url.find("currency=usd") != std::string::npos);
    CHECK(url.find("token") == std::string::npos);
}

TEST_CASE("Travelpayouts: answers are cached on disk and reused") {
    const std::string sample = read_file(kFixtures + "/travelpayouts_prices_for_dates.json");
    const std::string cache = temp_path("cache.json");
    std::vector<FakeHttp::Call> calls;
    auto answer = [&](const std::string&) { return HttpResponse{200, sample}; };

    TravelpayoutsOptions o;
    o.token = "abc123TEST";
    o.cache_path = cache;
    {
        TravelpayoutsProvider p(o, std::make_unique<FakeHttp>(&calls, answer));
        CHECK(p.cheapest_fare("MAD", "BCN", d("2023-07-28")) == Cents{592900});
        CHECK_FALSE(p.cheapest_fare("MAD", "BCN", d("2023-07-29")));  // "no flight" is cached too
        CHECK(p.cheapest_fare("MAD", "BCN", d("2023-07-28")) == Cents{592900});
        CHECK(calls.size() == 2);
        CHECK(p.cache_hits() == 1);
        REQUIRE_FALSE(calls[0].headers.empty());
        CHECK(std::find(calls[0].headers.begin(), calls[0].headers.end(), "X-Access-Token: abc123TEST") !=
              calls[0].headers.end());
        CHECK(calls[0].url.find("abc123TEST") == std::string::npos);
    }  // the destructor saves the cache
    {
        TravelpayoutsProvider again(o, std::make_unique<FakeHttp>(&calls, answer));
        CHECK(again.cheapest_fare("MAD", "BCN", d("2023-07-28")) == Cents{592900});
        CHECK_FALSE(again.cheapest_fare("MAD", "BCN", d("2023-07-29")));
        CHECK(again.requests_made() == 0);
        CHECK(calls.size() == 2);
    }
    std::filesystem::remove(cache);
}

TEST_CASE("Travelpayouts: request budget, failures and a rejected token") {
    std::vector<FakeHttp::Call> calls;
    TravelpayoutsOptions o;
    o.token = "abc123";
    o.cache_path = "";  // no disk cache

    SUBCASE("budget") {
        o.max_requests = 1;
        TravelpayoutsProvider p(o, std::make_unique<FakeHttp>(&calls, [](const std::string&) {
            return HttpResponse{200, R"({"success":true,"data":[]})"};
        }));
        CHECK_FALSE(p.cheapest_fare("YUL", "CDG", d("2027-05-01")));
        CHECK_FALSE(p.cheapest_fare("YUL", "CDG", d("2027-05-02")));
        CHECK(calls.size() == 1);
        CHECK(p.skipped() == 1);
        bool warned = false;
        for (const auto& line : p.notes()) warned = warned || line.find("stopped asking") != std::string::npos;
        CHECK(warned);
    }
    SUBCASE("a failed lookup counts as no flight and is reported") {
        TravelpayoutsProvider p(o, std::make_unique<FakeHttp>(&calls, [](const std::string&) {
            return HttpResponse{503, "busy"};
        }));
        CHECK_FALSE(p.cheapest_fare("YUL", "CDG", d("2027-05-01")));
        CHECK(p.failures() == 1);
    }
    SUBCASE("five failures in a row stop the search") {
        TravelpayoutsProvider p(o, std::make_unique<FakeHttp>(&calls, [](const std::string&) {
            return HttpResponse{0, "curl: (6) Could not resolve host"};
        }));
        for (int i = 1; i <= 4; ++i) CHECK_FALSE(p.cheapest_fare("YUL", "CDG", d("2027-05-01").plus_days(i)));
        CHECK_THROWS_AS(p.cheapest_fare("YUL", "CDG", d("2027-05-06")), FareError);
    }
    SUBCASE("rejected token") {
        TravelpayoutsProvider p(o, std::make_unique<FakeHttp>(&calls, [](const std::string&) {
            return HttpResponse{401, "Unauthorized"};
        }));
        CHECK_THROWS_WITH_AS(p.cheapest_fare("YUL", "CDG", d("2027-05-01")), doctest::Contains("rejected the token"),
                             FareError);
    }
    SUBCASE("missing or odd token") {
        o.token = "";
        CHECK_THROWS_AS(TravelpayoutsProvider{o}, FareError);
        o.token = "abc\" & del *";
        CHECK_THROWS_AS(TravelpayoutsProvider{o}, FareError);
    }
}

TEST_CASE("disk cache drops entries older than the time-to-live") {
    const std::string path = temp_path("ttl.json");
    std::int64_t now = 1'800'000'000;
    auto clock = [&] { return now; };
    {
        DiskCache c(path, 24 * 3600, clock);
        c.put("a", {true, 12345});
        c.put("b", {false, 0});
        REQUIRE(c.save());
    }
    now += 3600;
    {
        DiskCache c(path, 24 * 3600, clock);
        REQUIRE(c.get("a"));
        CHECK(c.get("a")->price == 12345);
        REQUIRE(c.get("b"));
        CHECK_FALSE(c.get("b")->has_flight);
    }
    now += 24 * 3600;
    {
        DiskCache c(path, 24 * 3600, clock);
        CHECK_FALSE(c.get("a"));
        CHECK(c.size() == 0);
    }
    {
        std::ofstream(path) << "not json";
        DiskCache c(path, 24 * 3600, clock);
        CHECK(c.size() == 0);
        CHECK_FALSE(c.warning().empty());
        c.put("c", {true, 777});
        CHECK(c.save());  // replaces the corrupt file
    }
    {
        DiskCache c(path, 24 * 3600, clock);
        REQUIRE(c.get("c"));
        CHECK(c.get("c")->price == 777);
        CHECK(c.warning().empty());
    }
    std::filesystem::remove(path);
}
