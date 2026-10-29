// Prices are kept as whole cents in a 64-bit integer so that sums and
// comparisons are exact (no floating point drift when legs are added up).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace flighto {

using Cents = std::int64_t;

inline Cents to_cents(double amount) {
    return static_cast<Cents>(std::llround(amount * 100.0));
}

// 123456 -> "1234.56"
inline std::string format_money(Cents cents) {
    const bool negative = cents < 0;
    const unsigned long long abs = negative ? 0ULL - static_cast<unsigned long long>(cents)
                                            : static_cast<unsigned long long>(cents);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s%llu.%02llu", negative ? "-" : "", abs / 100, abs % 100);
    return buf;
}

}  // namespace flighto
