// Calendar dates without time zones, stored as a day count.
//
// Flights are planned by calendar day, so a Date is just "days since
// 1970-01-01" with conversions to and from YYYY-MM-DD. Day math is plain
// integer math on that count.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace flighto {

bool is_leap_year(int year);
int days_in_month(int year, int month);  // month is 1..12

class Date {
public:
    Date() = default;  // 1970-01-01

    // Throws std::invalid_argument for an impossible date (2027-02-29, month 13, ...).
    static Date from_ymd(int year, int month, int day);

    // Strict "YYYY-MM-DD". Returns std::nullopt for anything else.
    static std::optional<Date> try_parse(std::string_view text);

    // Like try_parse, but throws std::invalid_argument with a readable message.
    static Date parse(std::string_view text);

    int year() const;
    int month() const;
    int day() const;

    long days_since_epoch() const { return days_; }
    Date plus_days(long n) const { return Date(days_ + n); }

    std::string iso() const;           // "2027-05-01"
    const char* weekday_name() const;  // "Mon", "Tue", ...

    friend long operator-(const Date& a, const Date& b) { return a.days_ - b.days_; }
    friend bool operator==(const Date& a, const Date& b) { return a.days_ == b.days_; }
    friend bool operator!=(const Date& a, const Date& b) { return a.days_ != b.days_; }
    friend bool operator<(const Date& a, const Date& b) { return a.days_ < b.days_; }
    friend bool operator<=(const Date& a, const Date& b) { return a.days_ <= b.days_; }
    friend bool operator>(const Date& a, const Date& b) { return a.days_ > b.days_; }
    friend bool operator>=(const Date& a, const Date& b) { return a.days_ >= b.days_; }

private:
    explicit Date(long days) : days_(days) {}
    long days_ = 0;
};

}  // namespace flighto
