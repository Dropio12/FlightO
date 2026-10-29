#include "date.h"

#include <cstdio>
#include <stdexcept>

namespace flighto {

namespace {

// Howard Hinnant's days_from_civil / civil_from_days (public domain),
// valid for the whole proleptic Gregorian calendar.
long days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = static_cast<long>(y) - era * 400;                  // [0, 399]
    const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   // [0, 365]
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
    return era * 146097 + doe - 719468;
}

void civil_from_days(long z, int& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;                                     // [0, 146096]
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);              // [0, 365]
    const long mp = (5 * doy + 2) / 153;                                   // [0, 11]
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

bool all_digits(std::string_view s) {
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return !s.empty();
}

int to_int(std::string_view s) {
    int v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v;
}

}  // namespace

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap_year(year)) return 29;
    return kDays[month - 1];
}

Date Date::from_ymd(int year, int month, int day) {
    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
        throw std::invalid_argument(std::string("not a real date: ") + buf);
    }
    return Date(days_from_civil(year, month, day));
}

std::optional<Date> Date::try_parse(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    const auto ys = text.substr(0, 4), ms = text.substr(5, 2), ds = text.substr(8, 2);
    if (!all_digits(ys) || !all_digits(ms) || !all_digits(ds)) return std::nullopt;
    const int y = to_int(ys), m = to_int(ms), d = to_int(ds);
    if (m < 1 || m > 12 || d < 1 || d > days_in_month(y, m)) return std::nullopt;
    return Date(days_from_civil(y, m, d));
}

Date Date::parse(std::string_view text) {
    if (auto d = try_parse(text)) return *d;
    throw std::invalid_argument("expected a date like 2027-05-01, got '" + std::string(text) + "'");
}

int Date::year() const {
    int y, m, d;
    civil_from_days(days_, y, m, d);
    return y;
}

int Date::month() const {
    int y, m, d;
    civil_from_days(days_, y, m, d);
    return m;
}

int Date::day() const {
    int y, m, d;
    civil_from_days(days_, y, m, d);
    return d;
}

std::string Date::iso() const {
    int y, m, d;
    civil_from_days(days_, y, m, d);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    return buf;
}

const char* Date::weekday_name() const {
    static const char* kNames[7] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};  // 1970-01-01 was a Thursday
    long r = days_ % 7;
    if (r < 0) r += 7;
    return kNames[r];
}

}  // namespace flighto
