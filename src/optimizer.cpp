#include "optimizer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace flighto {

namespace {

constexpr Cents kInf = std::numeric_limits<Cents>::max();

// Memoized fare lookups: a dense table over (from place, to place, day).
// Places 0..n-1 are the cities and place n is home. Each cell is asked of
// the provider at most once.
class FareMemo {
public:
    FareMemo(std::vector<std::string> places, Date day0, int days, FareProvider& provider)
        : places_(std::move(places)),
          day0_(day0),
          days_(days),
          provider_(provider),
          cells_(places_.size() * places_.size() * static_cast<std::size_t>(days), kUnknown) {}

    // Price in cents, or kNone when there is no flight that day.
    Cents get(int from, int to, int day) {
        Cents& cell = cells_[(static_cast<std::size_t>(from) * places_.size() + static_cast<std::size_t>(to)) *
                                 static_cast<std::size_t>(days_) +
                             static_cast<std::size_t>(day)];
        if (cell == kUnknown) {
            ++lookups_;
            const auto fare = provider_.cheapest_fare(places_[from], places_[to], day0_.plus_days(day));
            if (!fare) {
                cell = kNone;
                ++missing_;
            } else if (*fare < 0) {
                throw FareError("provider returned a negative price for " + places_[from] + " -> " +
                                places_[to] + " on " + day0_.plus_days(day).iso());
            } else {
                cell = *fare;
            }
        }
        return cell;
    }

    std::size_t lookups() const { return lookups_; }
    std::size_t missing() const { return missing_; }

    static constexpr Cents kNone = -1;

private:
    static constexpr Cents kUnknown = -2;

    std::vector<std::string> places_;
    Date day0_;
    int days_;
    FareProvider& provider_;
    std::vector<Cents> cells_;
    std::size_t lookups_ = 0;
    std::size_t missing_ = 0;
};

// One partial trip stored in a DP state: its cost and where it came from.
struct Entry {
    Cents cost = kInf;
    std::int32_t prev_state = -1;  // -1: this was the first flight, from home
    std::int32_t prev_rank = 0;    // which of the previous state's K entries
};

// A complete trip: the state of the last city plus the return flight day.
struct Final {
    Cents cost = kInf;
    std::int32_t state = -1;
    std::int32_t rank = 0;
    int return_day = -1;  // -1 when the trip does not return home
};

// Insert into a list of K entries sorted by cost, dropping the worst.
// Ties keep the earlier entry first, so results are deterministic.
template <typename T>
void offer(T* list, int k, const T& candidate) {
    if (candidate.cost >= list[k - 1].cost) return;
    int pos = k - 1;
    while (pos > 0 && list[pos - 1].cost > candidate.cost) {
        list[pos] = list[pos - 1];
        --pos;
    }
    list[pos] = candidate;
}

}  // namespace

SearchResult find_cheapest_trips(const TripRequest& req, FareProvider& provider) {
    validate(req);
    const auto started = std::chrono::steady_clock::now();

    const int n = static_cast<int>(req.cities.size());
    const int home = n;
    const int W = static_cast<int>(req.latest_return - req.earliest_departure);  // last day index
    const int D = W + 1;
    const int K = 1 + req.alternatives;
    const std::uint32_t full = (1u << n) - 1;

    const std::size_t states = (std::size_t(1) << n) * static_cast<std::size_t>(n) * static_cast<std::size_t>(D);
    const std::size_t bytes = states * static_cast<std::size_t>(K) * sizeof(Entry);
    if (bytes > kMaxTableBytes) {
        throw std::invalid_argument("this search would need about " + std::to_string(bytes >> 20) +
                                    " MB; use fewer cities, a shorter window or fewer alternatives");
    }

    std::vector<std::string> places;
    for (const auto& c : req.cities) places.push_back(c.code);
    places.push_back(req.home);
    FareMemo fares(places, req.earliest_departure, D, provider);

    // rest_min[S] = minimum nights still owed to the cities not in S.
    std::vector<int> rest_min(std::size_t(1) << n, 0);
    for (std::uint32_t mask = 0; mask <= full; ++mask) {
        for (int j = 0; j < n; ++j) {
            if (!(mask & (1u << j))) rest_min[mask] += req.cities[j].min_nights;
        }
    }
    // Can a trip that just landed in city i on day t (having visited S)
    // still fit every remaining minimum stay before the end of the window?
    auto can_finish = [&](std::uint32_t mask, int i, int t) {
        return t + req.cities[i].min_nights + rest_min[mask] <= W;
    };
    auto index = [&](std::uint32_t mask, int i, int t) {
        return (static_cast<std::size_t>(mask) * static_cast<std::size_t>(n) + static_cast<std::size_t>(i)) *
                   static_cast<std::size_t>(D) +
               static_cast<std::size_t>(t);
    };

    std::vector<Entry> table(states * static_cast<std::size_t>(K));
    auto slots = [&](std::size_t state) { return &table[state * static_cast<std::size_t>(K)]; };

    // First flight: home -> j on day t.
    for (int j = 0; j < n; ++j) {
        const std::uint32_t mask = 1u << j;
        for (int t = 0; t <= W && can_finish(mask, j, t); ++t) {
            const Cents f = fares.get(home, j, t);
            if (f == FareMemo::kNone) continue;
            offer(slots(index(mask, j, t)), K, Entry{f, -1, 0});
        }
    }

    std::vector<Final> finals(static_cast<std::size_t>(K));
    std::size_t reached = 0;

    // Relax every state in increasing day order. All moves go to a later
    // day, so a state is final by the time its day comes up.
    for (int t = 0; t <= W; ++t) {
        for (std::uint32_t mask = 1; mask <= full; ++mask) {
            for (int i = 0; i < n; ++i) {
                if (!(mask & (1u << i))) continue;
                const std::size_t state = index(mask, i, t);
                const Entry* here = slots(state);
                if (here[0].cost == kInf) continue;
                ++reached;
                const CityStay& city = req.cities[i];

                if (mask == full) {
                    if (!req.return_home) {
                        for (int r = 0; r < K && here[r].cost != kInf; ++r) {
                            offer(finals.data(), K, Final{here[r].cost, static_cast<std::int32_t>(state), r, -1});
                        }
                        continue;
                    }
                    for (int nights = city.min_nights; nights <= city.max_nights && t + nights <= W; ++nights) {
                        const int day = t + nights;
                        const Cents f = fares.get(i, home, day);
                        if (f == FareMemo::kNone) continue;
                        for (int r = 0; r < K && here[r].cost != kInf; ++r) {
                            offer(finals.data(), K, Final{here[r].cost + f, static_cast<std::int32_t>(state), r, day});
                        }
                    }
                    continue;
                }

                for (int nights = city.min_nights; nights <= city.max_nights && t + nights <= W; ++nights) {
                    const int day = t + nights;
                    for (int j = 0; j < n; ++j) {
                        if (mask & (1u << j)) continue;
                        const std::uint32_t next_mask = mask | (1u << j);
                        if (!can_finish(next_mask, j, day)) continue;
                        const Cents f = fares.get(i, j, day);
                        if (f == FareMemo::kNone) continue;
                        Entry* there = slots(index(next_mask, j, day));
                        for (int r = 0; r < K && here[r].cost != kInf; ++r) {
                            const Cents cost = here[r].cost + f;
                            if (cost >= there[K - 1].cost) break;  // later ranks cost even more
                            offer(there, K, Entry{cost, static_cast<std::int32_t>(state), r});
                        }
                    }
                }
            }
        }
    }

    // Walk the back pointers of each complete trip to list its flights.
    const auto days = static_cast<std::size_t>(D);
    auto city_of = [&](std::int32_t state) { return static_cast<int>((static_cast<std::size_t>(state) / days) % static_cast<std::size_t>(n)); };
    auto day_of = [&](std::int32_t state) { return static_cast<int>(static_cast<std::size_t>(state) % days); };
    auto entry_of = [&](std::int32_t state, std::int32_t rank) -> const Entry& {
        return slots(static_cast<std::size_t>(state))[rank];
    };

    SearchResult result;
    for (const Final& fin : finals) {
        if (fin.cost == kInf) break;
        Itinerary trip;
        trip.total = fin.cost;
        trip.ends_at_home = req.return_home;

        std::vector<Leg> reversed;
        std::int32_t state = fin.state;
        std::int32_t rank = fin.rank;
        int next_day = fin.return_day;
        if (req.return_home) {
            reversed.push_back(Leg{req.cities[city_of(state)].code, req.home,
                                   req.earliest_departure.plus_days(fin.return_day),
                                   fin.cost - entry_of(state, rank).cost, 0});
        }
        while (state != -1) {
            const int city = city_of(state);
            const int day = day_of(state);
            const Entry& e = entry_of(state, rank);
            Cents before = 0;
            std::string from = req.home;
            if (e.prev_state != -1) {
                before = entry_of(e.prev_state, e.prev_rank).cost;
                from = req.cities[city_of(e.prev_state)].code;
            }
            const int nights = next_day >= 0 ? next_day - day : 0;
            reversed.push_back(Leg{from, req.cities[city].code, req.earliest_departure.plus_days(day), e.cost - before,
                                   nights});
            next_day = day;
            state = e.prev_state;
            rank = e.prev_rank;
        }
        trip.legs.assign(reversed.rbegin(), reversed.rend());
        result.itineraries.push_back(std::move(trip));
    }

    result.stats.fare_lookups = fares.lookups();
    result.stats.missing_fares = fares.missing();
    result.stats.states_total = states;
    result.stats.states_reached = reached;
    result.stats.millis =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

}  // namespace flighto
