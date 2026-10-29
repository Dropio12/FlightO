// Finds the cheapest multi-city trip, plus the next-cheapest alternatives.
//
// The problem
// -----------
// Start at `home` on or after `earliest_departure`, visit every city exactly
// once in any order, stay between min and max nights in each, and (unless
// `return_home` is false) fly back home on or before `latest_return`. Each
// flight costs whatever the fare provider says for that (from, to, date); a
// missing fare means there is no flight that day. Minimize the total.
//
// The method: Held-Karp dynamic programming with a day axis
// ---------------------------------------------------------
// A state is (S, i, t): the set S of cities already visited, the city i we
// just flew into (i is in S), and the day t we landed there (days counted
// from earliest_departure). dp[S][i][t] is the cheapest way to reach that
// state. From (S, i, t) we stay s nights, min_i <= s <= max_i, and fly on
// day t + s to a city j not in S:
//
//   dp[S + {j}][j][t + s] = min(dp[S][i][t] + fare(i, j, t + s))
//
// The first flight seeds dp[{j}][j][t] = fare(home, j, t). When S holds every
// city, the trip ends with fare(i, home, t + s) (or right there when
// return_home is false). Every move goes strictly forward in time (s >= 1),
// so relaxing states in increasing day order visits each state after all of
// its predecessors. That makes the answer exact for the fares given: it is
// the same answer as trying every order, start day and stay length.
//
// To return alternatives, each state keeps its K cheapest distinct partial
// trips instead of one (K = 1 + alternatives). The k-th best full trip always
// extends one of the K best partial trips of the state before its last
// flight, so K entries per state are enough.
//
// States that cannot finish in time are skipped: from (S, i, t) we still need
// at least min_i nights at i plus the minimum nights of every city not in S,
// all before latest_return. Their fares are never looked up.
//
// Complexity
// ----------
// n = number of cities, W = days in the window, R = widest stay range
// (max - min + 1), K = 1 + alternatives.
//   states:  2^n * n * (W + 1)
//   time:    O(2^n * n^2 * W * R * K)
//   memory:  O(2^n * n * W * K), 16 bytes per entry. For n = 8, W = 90, K = 3
//            that is about 9 MB and the search takes milliseconds.
//   fares:   at most (n + 1) * n * (W + 1) distinct (from, to, date) lookups,
//            each asked of the provider once (memoized), usually far fewer.
// Trying every order instead costs n! * (start days) * R^n.
#pragma once

#include <cstddef>
#include <vector>

#include "fare_provider.h"
#include "trip.h"

namespace flighto {

struct SearchStats {
    std::size_t fare_lookups = 0;    // distinct (from, to, date) asked of the provider
    std::size_t missing_fares = 0;   // lookups that found no flight
    std::size_t states_total = 0;    // size of the DP table (per alternative slot)
    std::size_t states_reached = 0;  // states with at least one partial trip
    double millis = 0;               // wall time of the search, lookups included
};

struct SearchResult {
    std::vector<Itinerary> itineraries;  // cheapest first; empty if no trip is possible
    SearchStats stats;
};

// Upper bound on the DP table size. Requests above it are refused with
// std::invalid_argument before any memory is allocated.
constexpr std::size_t kMaxTableBytes = std::size_t(512) * 1024 * 1024;

// Validates the request (see validate() in trip.h), then searches.
// Throws std::invalid_argument for a bad request and lets FareError from the
// provider pass through.
SearchResult find_cheapest_trips(const TripRequest& request, FareProvider& provider);

}  // namespace flighto
