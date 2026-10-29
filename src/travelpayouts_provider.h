// Live fares from the Travelpayouts (Aviasales) Data API.
//
// Endpoint: GET https://api.travelpayouts.com/aviasales/v3/prices_for_dates
//   origin, destination  IATA codes
//   departure_at         YYYY-MM-DD (the day of the flight)
//   one_way=true         one-way tickets; the API then returns the cheapest per date
//   currency             e.g. usd
// The token goes in the X-Access-Token header, never in the URL. A free
// token comes with a Travelpayouts account (Profile > API token).
//
// These are real prices that Aviasales users found in the last 48 hours,
// served from Aviasales' cache. A date with no cached ticket comes back
// empty, which FlightO treats as "no flight that day". The documented rate
// limit for this method is 600 requests per minute.
//
// Every answer, including "no flight", is saved in a small JSON file so the
// next run asks the API only for what it does not know yet.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "disk_cache.h"
#include "fare_provider.h"
#include "http.h"

namespace flighto {

struct TravelpayoutsOptions {
    std::string token;                              // from the TRAVELPAYOUTS_TOKEN environment variable
    std::string currency = "USD";
    std::string cache_path = "flighto_cache.json";  // empty: do not cache on disk
    double cache_ttl_hours = 24;
    int max_requests = 600;                         // per run; protects the free quota
};

class TravelpayoutsProvider : public FareProvider {
public:
    // Throws FareError if the token is missing or has unexpected characters.
    // `http` defaults to CurlHttpClient; tests pass a fake.
    explicit TravelpayoutsProvider(TravelpayoutsOptions options, std::unique_ptr<HttpClient> http = nullptr);
    ~TravelpayoutsProvider() override;  // saves the disk cache

    // Throws FareError when the token is rejected (HTTP 401/403) or when 5
    // requests in a row fail, since every later lookup would fail too.
    // Other failures count as "no flight" and are reported by notes().
    std::optional<Cents> cheapest_fare(const std::string& from, const std::string& to,
                                       const Date& date) override;
    std::string describe() const override;
    std::vector<std::string> notes() const override;

    void save_cache() { cache_.save(); }

    std::size_t requests_made() const { return requests_; }
    std::size_t cache_hits() const { return cache_hits_; }
    std::size_t failures() const { return failures_; }
    std::size_t skipped() const { return skipped_; }

    // The request URL, without the token.
    static std::string endpoint_url(const std::string& from, const std::string& to, const Date& date,
                                    const std::string& currency);

    // Cheapest price in a prices_for_dates response for flights leaving on
    // `date` (local date of departure_at). std::nullopt when there is none.
    // Throws FareError for "success": false, non-JSON text, or prices in
    // another currency than `currency`.
    static std::optional<Cents> parse_response(const std::string& body, const Date& date,
                                               const std::string& currency);

private:
    TravelpayoutsOptions options_;
    std::unique_ptr<HttpClient> http_;
    DiskCache cache_;
    std::size_t requests_ = 0;
    std::size_t cache_hits_ = 0;
    std::size_t failures_ = 0;
    std::size_t failures_in_a_row_ = 0;
    std::size_t skipped_ = 0;
    std::string last_error_;
};

}  // namespace flighto
