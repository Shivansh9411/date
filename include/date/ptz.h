#include <stdexcept>
#include <cstdlib>
#ifndef PTZ_H
#define PTZ_H

// The MIT License (MIT)
//
// Copyright (c) 2017 Howard Hinnant
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// This header allows Posix-style time zones as specified for TZ here:
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
//
// Posix::time_zone can be constructed with a posix-style string and then used in
// a zoned_time like so:
//
// zoned_time<system_clock::duration, Posix::time_zone> zt{"EST5EDT,M3.2.0,M11.1.0",
//                                                         system_clock::now()};
// or:
//
// Posix::time_zone tz{"EST5EDT,M3.2.0,M11.1.0"};
// zoned_time<system_clock::duration, Posix::time_zone> zt{tz, system_clock::now()};
//
// In C++17 CTAD simplifies this to:
//
// Posix::time_zone tz{"EST5EDT,M3.2.0,M11.1.0"};
// zoned_time zt{tz, system_clock::now()};
//
// Extension to the Posix rules to allow a constant daylight saving offset:
//
// If the rule set is missing (everything starting with ','), then
// there must be exactly one abbreviation (std or daylight) with
// length 3 or greater, and that will be used as the constant offset. If
// there are two, the std abbreviation is silently set to "", and the
// result is constant daylight saving. If there are zero abbreviations
// with no rule set, an exception is thrown.
//
// Example:
// "EST5" yields a constant offset of -5h with 0h save and "EST abbreviation.
// "5EDT" yields a constant offset of -4h with 1h save and "EDT" abbreviation.
// "EST5EDT" and "5EDT4" are both equal to "5EDT".
//
// Note, Posix-style time zones are not recommended for all of the reasons described here:
// https://stackoverflow.com/tags/timezone/info
//
// They are provided here as a non-trivial custom time zone example, and if you really
// have to have Posix time zones, you're welcome to use this one.

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <ostream>
#include <string>

#ifndef HAS_CHRONO_20
#  if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION <= 200100
#    define HAS_CHRONO_20 0
#  else
#    define HAS_CHRONO_20 1
#  endif
#endif

#if HAS_CHRONO_20
   namespace chr = std::chrono;
#  define HAS_STRING_VIEW 1
#  include <format>
#else
#  include "date/tz.h"
   namespace chr = date;
#endif

namespace Posix
{

namespace detail
{

#if HAS_STRING_VIEW

using string_t = std::string_view;

#else  // !HAS_STRING_VIEW

using string_t = std::string;

#endif  // !HAS_STRING_VIEW

class rule;

void throw_invalid(const string_t& s, unsigned i, const string_t& message);
unsigned read_date(const string_t& s, unsigned i, rule& r);
unsigned read_name(const string_t& s, unsigned i, std::string& name);
unsigned read_signed_time(const string_t& s, unsigned i, std::chrono::seconds& t);
unsigned read_unsigned_time(const string_t& s, unsigned i, std::chrono::seconds& t);
unsigned read_unsigned(const string_t& s, unsigned i,  unsigned limit, unsigned& u,
                       const string_t& message = string_t{});

class rule
{
    enum {off, J, M, N};

    chr::month m_;
    chr::weekday wd_;
    unsigned short n_    : 14;
    unsigned short mode_ : 2;
    std::chrono::duration<std::int32_t> time_ = std::chrono::hours{2};

public:
    rule() : mode_(off) {
    __builtin_trap() /* STUB: not implemented */;
}

    bool ok() const {
    __builtin_trap() /* STUB: not implemented */;
}
    chr::local_seconds operator()(chr::year y) const;
    std::string to_string() const;

    friend std::ostream& operator<<(std::ostream& os, const rule& r);
    friend unsigned read_date(const string_t& s, unsigned i, rule& r);
    friend bool operator==(const rule& x, const rule& y);
};

inline
bool
operator==(const rule& x, const rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
bool
operator!=(const rule& x, const rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::local_seconds
rule::operator()(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
std::string
rule::to_string() const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
std::ostream&
operator<<(std::ostream& os, const rule& r)
{
    __builtin_trap() /* STUB: not implemented */;
}

}  // namespace detail

class time_zone
{
    std::string          std_abbrev_;
    std::string          dst_abbrev_ = {};
    std::chrono::seconds offset_;
    std::chrono::seconds save_ = std::chrono::hours{1};
    detail::rule         start_rule_;
    detail::rule         end_rule_;

public:
    explicit time_zone(const detail::string_t& name);

    template <class Duration>
        chr::sys_info   get_info(chr::sys_time<Duration> st) const;
    template <class Duration>
        chr::local_info get_info(chr::local_time<Duration> tp) const;

    template <class Duration>
        chr::sys_time<typename std::common_type<Duration, std::chrono::seconds>::type>
        to_sys(chr::local_time<Duration> tp) const;

    template <class Duration>
        chr::sys_time<typename std::common_type<Duration, std::chrono::seconds>::type>
        to_sys(chr::local_time<Duration> tp, chr::choose z) const;

    template <class Duration>
        chr::local_time<typename std::common_type<Duration, std::chrono::seconds>::type>
        to_local(chr::sys_time<Duration> tp) const;

    friend std::ostream& operator<<(std::ostream& os, const time_zone& z);

    const time_zone* operator->() const {
    __builtin_trap() /* STUB: not implemented */;
}

    std::string name() const;

    friend bool operator==(const time_zone& x, const time_zone& y);

private:
    chr::sys_seconds get_start(chr::year y) const;
    chr::sys_seconds get_prev_start(chr::year y) const;
    chr::sys_seconds get_next_start(chr::year y) const;
    chr::sys_seconds get_end(chr::year y) const;
    chr::sys_seconds get_prev_end(chr::year y) const;
    chr::sys_seconds get_next_end(chr::year y) const;
    chr::sys_info contant_offset() const;
};

inline
chr::sys_seconds
time_zone::get_start(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_seconds
time_zone::get_prev_start(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_seconds
time_zone::get_next_start(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_seconds
time_zone::get_end(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_seconds
time_zone::get_prev_end(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_seconds
time_zone::get_next_end(chr::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
chr::sys_info
time_zone::contant_offset() const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
time_zone::time_zone(const detail::string_t& s)
{
    __builtin_trap() /* STUB: not implemented */;
}

template <class Duration>
chr::sys_info
time_zone::get_info(chr::sys_time<Duration> st) const
{
    using chr::sys_info;
    using chr::year_month_day;
    using chr::sys_days;
    using chr::floor;
    using chr::ceil;
    using chr::days;
    using chr::year;
    using chr::January;
    using chr::December;
    using chr::last;
    using std::chrono::minutes;
    sys_info r{};
    r.offset = offset_;
    if (start_rule_.ok())
    {
        auto y = year_month_day{floor<days>(st)}.year();
        if (st >= get_next_start(y))
            ++y;
        else if (st < get_prev_end(y))
            --y;
        auto start = get_start(y);
        auto end   = get_end(y);
        if (start <= end)  // (northern hemisphere)
        {
            if (start <= st && st < end)
            {
                r.begin = start;
                r.end = end;
                r.offset += save_;
                r.save = ceil<minutes>(save_);
                r.abbrev = dst_abbrev_;
            }
            else if (st < start)
            {
                r.begin = get_prev_end(y);
                r.end = start;
                r.abbrev = std_abbrev_;
            }
            else  // st >= end
            {
                r.begin = end;
                r.end = get_next_start(y);
                r.abbrev = std_abbrev_;
            }
        }
        else  // end < start (southern hemisphere)
        {
            if (end <= st && st < start)
            {
                r.begin = end;
                r.end = start;
                r.abbrev = std_abbrev_;
            }
            else if (st < end)
            {
                r.begin = get_prev_start(y);
                r.end = end;
                r.offset += save_;
                r.save = ceil<minutes>(save_);
                r.abbrev = dst_abbrev_;
            }
            else  // st >= start
            {
                r.begin = start;
                r.end = get_next_end(y);
                r.offset += save_;
                r.save = ceil<minutes>(save_);
                r.abbrev = dst_abbrev_;
            }
        }
    }
    else
        r = contant_offset();
    using seconds = std::chrono::seconds;
    assert(r.begin <= floor<seconds>(st) && floor<seconds>(st) <= r.end);
    return r;
}

template <class Duration>
chr::local_info
time_zone::get_info(chr::local_time<Duration> tp) const
{
    using chr::local_info;
    using chr::year_month_day;
    using chr::days;
    using chr::sys_days;
    using chr::sys_seconds;
    using chr::year;
    using chr::ceil;
    using chr::January;
    using chr::December;
    using chr::last;
    using std::chrono::seconds;
    using std::chrono::minutes;
    local_info r{};
    using chr::floor;
    if (start_rule_.ok())
    {
        auto y = year_month_day{floor<days>(tp)}.year();
        auto start = get_start(y);
        auto end   = get_end(y);
        auto utcs = sys_seconds{floor<seconds>(tp - offset_).time_since_epoch()};
        auto utcd = sys_seconds{floor<seconds>(tp - (offset_ + save_)).time_since_epoch()};
        auto northern = start <= end;
        if ((utcs < start) != (utcd < start))
        {
            if (northern)
                r.first.begin = get_prev_end(y);
            else
                r.first.begin = end;
            r.first.end = start;
            r.first.offset = offset_;
            r.first.abbrev = std_abbrev_;
            r.second.begin = start;
            if (northern)
                r.second.end = end;
            else
                r.second.end = get_next_end(y);
            r.second.abbrev = dst_abbrev_;
            r.second.offset = offset_ + save_;
            r.second.save = ceil<minutes>(save_);
            r.result = save_ > seconds{0} ? local_info::nonexistent
                                          : local_info::ambiguous;
        }
        else if ((utcs < end) != (utcd < end))
        {
            if (northern)
                r.first.begin = start;
            else
                r.first.begin = get_prev_start(y);
            r.first.end = end;
            r.first.offset = offset_ + save_;
            r.first.save = ceil<minutes>(save_);
            r.first.abbrev = dst_abbrev_;
            r.second.begin = end;
            if (northern)
                r.second.end = get_next_start(y);
            else
                r.second.end = start;
            r.second.abbrev = std_abbrev_;
            r.second.offset = offset_;
            r.result = save_ > seconds{0} ? local_info::ambiguous
                                          : local_info::nonexistent;
        }
        else
            r.first = get_info(utcs);
    }
    else
        r.first = contant_offset();
    return r;
}

template <class Duration>
chr::sys_time<typename std::common_type<Duration, std::chrono::seconds>::type>
time_zone::to_sys(chr::local_time<Duration> tp) const
{
    using chr::local_info;
    using chr::sys_time;
    using chr::ambiguous_local_time;
    using chr::nonexistent_local_time;
    auto i = get_info(tp);
    if (i.result == local_info::nonexistent)
        throw nonexistent_local_time(tp, i);
    else if (i.result == local_info::ambiguous)
        throw ambiguous_local_time(tp, i);
    return sys_time<Duration>{tp.time_since_epoch()} - i.first.offset;
}

template <class Duration>
chr::sys_time<typename std::common_type<Duration, std::chrono::seconds>::type>
time_zone::to_sys(chr::local_time<Duration> tp, chr::choose z) const
{
    using chr::local_info;
    using chr::sys_time;
    using chr::choose;
    auto i = get_info(tp);
    if (i.result == local_info::nonexistent)
    {
        return i.first.end;
    }
    else if (i.result == local_info::ambiguous)
    {
        if (z == choose::latest)
            return sys_time<Duration>{tp.time_since_epoch()} - i.second.offset;
    }
    return sys_time<Duration>{tp.time_since_epoch()} - i.first.offset;
}

template <class Duration>
chr::local_time<typename std::common_type<Duration, std::chrono::seconds>::type>
time_zone::to_local(chr::sys_time<Duration> tp) const
{
    using chr::local_time;
    using std::chrono::seconds;
    using LT = local_time<typename std::common_type<Duration, seconds>::type>;
    auto i = get_info(tp);
    return LT{(tp + i.offset).time_since_epoch()};
}

inline
std::ostream&
operator<<(std::ostream& os, const time_zone& z)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
std::string
time_zone::name() const
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
bool
operator==(const time_zone& x, const time_zone& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
bool
operator!=(const time_zone& x, const time_zone& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

namespace detail
{

inline
void
throw_invalid(const string_t& s, unsigned i, const string_t& message)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
unsigned
read_date(const string_t& s, unsigned i, rule& r)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
unsigned
read_name(const string_t& s, unsigned i, std::string& name)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
unsigned
read_signed_time(const string_t& s, unsigned i,
                                  std::chrono::seconds& t)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
unsigned
read_unsigned_time(const string_t& s, unsigned i, std::chrono::seconds& t)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
unsigned
read_unsigned(const string_t& s, unsigned i, unsigned limit, unsigned& u,
              const string_t& message)
{
    __builtin_trap() /* STUB: not implemented */;
}

}  // namespace detail

}  // namespace Posix

#if HAS_CHRONO_20
namespace std::chrono
#else
namespace date
#endif
{

template <>
struct zoned_traits<Posix::time_zone>
{

#if HAS_STRING_VIEW

    static
    Posix::time_zone
    locate_zone(std::string_view name)
    {
    __builtin_trap() /* STUB: not implemented */;
}

#else  // !HAS_STRING_VIEW

    static
    Posix::time_zone
    locate_zone(const std::string& name)
    {
        return Posix::time_zone{name};
    }

    static
    Posix::time_zone
    locate_zone(const char* name)
    {
        return Posix::time_zone{name};
    }

#endif  // !HAS_STRING_VIEW

};

}  // namespace chr

#endif  // PTZ_H
