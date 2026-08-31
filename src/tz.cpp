#include <stdexcept>
// The MIT License (MIT)
//
// Copyright (c) 2015, 2016, 2017 Howard Hinnant
// Copyright (c) 2015 Ville Voutilainen
// Copyright (c) 2016 Alexander Kormanovsky
// Copyright (c) 2016, 2017 Jiangang Zhuang
// Copyright (c) 2017 Nicolas Veloz Savino
// Copyright (c) 2017 Florian Dang
// Copyright (c) 2017 Aaron Bishop
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
//
// Our apologies.  When the previous paragraph was written, lowercase had not yet
// been invented (that would involve another several millennia of evolution).
// We did not mean to shout.

#ifdef _WIN32
   // windows.h will be included directly and indirectly (e.g. by curl).
   // We need to define these macros to prevent windows.h bringing in
   // more than we need and do it early so windows.h doesn't get included
   // without these macros having been defined.
   // min/max macros interfere with the C++ versions.
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
   // We don't need all that Windows has to offer.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

   // for wcstombs
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif

   // None of this happens with the MS SDK (at least VS14 which I tested), but:
   // Compiling with mingw, we get "error: 'KF_FLAG_DEFAULT' was not declared in this scope."
   // and error: 'SHGetKnownFolderPath' was not declared in this scope.".
   // It seems when using mingw NTDDI_VERSION is undefined and that
   // causes KNOWN_FOLDER_FLAG and the KF_ flags to not get defined.
   // So we must define NTDDI_VERSION to get those flags on mingw.
   // The docs say though here:
   // https://msdn.microsoft.com/en-nz/library/windows/desktop/aa383745(v=vs.85).aspx
   // that "If you define NTDDI_VERSION, you must also define _WIN32_WINNT."
   // So we declare we require Vista or greater.
#  ifdef __MINGW32__

#    ifndef NTDDI_VERSION
#      define NTDDI_VERSION 0x06000000
#      define _WIN32_WINNT _WIN32_WINNT_VISTA
#    elif NTDDI_VERSION < 0x06000000
#      warning "If this fails to compile NTDDI_VERSION may be to low. See comments above."
#    endif
     // But once we define the values above we then get this linker error:
     // "tz.cpp:(.rdata$.refptr.FOLDERID_Downloads[.refptr.FOLDERID_Downloads]+0x0): "
     //     "undefined reference to `FOLDERID_Downloads'"
     // which #include <initguid.h> cures see:
     // https://support.microsoft.com/en-us/kb/130869
#    include <initguid.h>
     // But with <initguid.h> included, the error moves on to:
     // error: 'FOLDERID_Downloads' was not declared in this scope
     // Which #include <knownfolders.h> cures.
#    include <knownfolders.h>

#  endif  // __MINGW32__

#  include <windows.h>
#endif  // _WIN32

#include "date/tz_private.h"

#ifdef __APPLE__
#  include "date/ios.h"
#else
#  define TARGET_OS_IPHONE 0
#  define TARGET_OS_SIMULATOR 0
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#  include <sys/system_properties.h>
#  if USE_OS_TZDB
#    define MISSING_LEAP_SECONDS 1
// from https://android.googlesource.com/platform/bionic/+/master/libc/tzcode/bionic.cpp
static constexpr size_t ANDROID_TIMEZONE_NAME_LENGTH = 40;
struct bionic_tzdata_header_t {
  char tzdata_version[12];
  std::int32_t index_offset;
  std::int32_t data_offset;
  std::int32_t final_offset;
};
struct index_entry_t {
  char buf[ANDROID_TIMEZONE_NAME_LENGTH];
  std::int32_t start;
  std::int32_t length;
  std::int32_t unused; // Was raw GMT offset; always 0 since tzdata2014f (L).
};
#  endif // USE_OS_TZDB
#endif // defined(ANDROID) || defined(__ANDROID__)

#if USE_OS_TZDB
#  include <dirent.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#if USE_OS_TZDB
#  include <queue>
#endif
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include <sys/stat.h>

// unistd.h is used on some platforms as part of the the means to get
// the current time zone. On Win32 windows.h provides a means to do it.
// gcc/mingw supports unistd.h on Win32 but MSVC does not.

#ifdef __ANDROID__
#  define INSTALL .
#endif
#ifdef _WIN32
#  ifdef WINAPI_FAMILY
#    include <winapifamily.h>
#    if WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP
#      define WINRT
#      define INSTALL .
#    endif
#  endif

#  include <io.h> // _unlink etc.

#  if defined(__clang__)
    struct IUnknown;    // fix for issue with static_cast<> in objbase.h
                        //   (see https://github.com/philsquared/Catch/issues/690)
#  endif

#  include <shlobj.h> // CoTaskFree, ShGetKnownFolderPath etc.
#  if HAS_REMOTE_API
#    include <direct.h> // _mkdir
#    include <shellapi.h> // ShFileOperation etc.
#  endif  // HAS_REMOTE_API
#else   // !_WIN32
#  include <unistd.h>
#  if !USE_OS_TZDB && !defined(INSTALL)
#    include <wordexp.h>
#  endif
#  include <limits.h>
#  include <string.h>
#  if !USE_SHELL_API
#    include <sys/stat.h>
#    include <sys/fcntl.h>
#    include <dirent.h>
#    include <cstring>
#    include <sys/wait.h>
#    include <sys/types.h>
#  endif //!USE_SHELL_API
#endif  // !_WIN32


#if HAS_REMOTE_API
   // Note curl includes windows.h so we must include curl AFTER definitions of things
   // that affect windows.h such as NOMINMAX.
#if defined(_MSC_VER) && defined(SHORTENED_CURL_INCLUDE)
   // For rmt_curl nuget package
#  include <curl.h>
#else
#  include <curl/curl.h>
#endif
#endif

#ifdef _WIN32
static CONSTDATA char folder_delimiter = '\\';
#elif !defined(ANDROID) && !defined(__ANDROID__)
static CONSTDATA char folder_delimiter = '/';
#endif  // !defined(WIN32) && !defined(ANDROID) && !defined(__ANDROID__)

#if defined(__GNUC__) && __GNUC__ < 5
   // GCC 4.9 Bug 61489 Wrong warning with -Wmissing-field-initializers
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif  // defined(__GNUC__) && __GNUC__ < 5

#if !USE_OS_TZDB

#  ifdef _WIN32

static
std::wstring
convert_utf8_to_utf16(const std::string& s)
{
    std::wstring out;
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);

    if (size == 0)
    {
        std::string msg = "Failed to determine required size when converting \"";
        msg += s;
        msg += "\" to UTF-16.";
        throw std::runtime_error(msg);
    }

    out.resize(size);
    const int check = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);

    if (size != check)
    {
        std::string msg = "Failed to convert \"";
        msg += s;
        msg += "\" to UTF-16.";
        throw std::runtime_error(msg);
    }

    return out;
}

#    ifndef WINRT

namespace
{
    struct task_mem_deleter
    {
        void operator()(wchar_t buf[])
        {
            if (buf != nullptr)
                CoTaskMemFree(buf);
        }
    };
    using co_task_mem_ptr = std::unique_ptr<wchar_t[], task_mem_deleter>;
}

// We might need to know certain locations even if not using the remote API,
// so keep these routines out of that block for now.
static
std::string
get_known_folder(const GUID& folderid)
{
    std::string folder;
    PWSTR pfolder = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folderid, KF_FLAG_DEFAULT, nullptr, &pfolder);
    if (SUCCEEDED(hr))
    {
        co_task_mem_ptr folder_ptr(pfolder);
        const wchar_t* fptr = folder_ptr.get();
        auto state = std::mbstate_t();
        const auto required = std::wcsrtombs(nullptr, &fptr, 0, &state);
        if (required != 0 && required != std::size_t(-1))
        {
            folder.resize(required);
            std::wcsrtombs(&folder[0], &fptr, folder.size(), &state);
        }
    }
    return folder;
}

#      ifndef INSTALL

// Usually something like "c:\Users\username\Downloads".
static
std::string
get_download_folder()
{
    return get_known_folder(FOLDERID_Downloads);
}

#      endif  // !INSTALL

#    endif // WINRT
#  else // !_WIN32

#    if !defined(INSTALL)

static
std::string
expand_path(std::string path)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::string
get_download_folder()
{
    __builtin_trap() /* STUB: not implemented */;
}

#    endif // !defined(INSTALL)

#  endif  // !_WIN32

/*
 * This class is provided to mimic the following usage of `ifstream`:
 *
 * std::ifstream is(filename);
 *
 * file_streambuf ibuf(filename);
 * std::istream is(&ibuf);
 *
 * This is required because `ifstream` does not support opening files
 * containing wide characters on Windows. On Windows, `file_streambuf` uses
 * `file_open()` to convert the file name to UTF-16 before opening it with
 * `_wfopen()`.
 *
 * Note that this is not an exact re-implementation of `ifstream`,
 * but is enough for usage here.
 *
 * It is partially based on these two implementations:
 * - fdinbuf from http://www.josuttis.com/cppcode/fdstream.html
 * - stdiobuf https://stackoverflow.com/questions/12342542/convert-file-to-ifstream-c-android-ndk
 *
 * Apparently MSVC provides non-standard overloads of `ifstream` that support
 * a `const wchar_t*` file name, but MinGW does not https://stackoverflow.com/a/822032
 */
class file_streambuf
  : public std::streambuf
{
private:
    FILE* file_;
    static const int buffer_size_ = 1024;
    char buffer_[buffer_size_];

public:
    ~file_streambuf()
    {
        if (file_)
        {
            ::fclose(file_);
        }
    }
    file_streambuf(const file_streambuf&) = delete;
    file_streambuf& operator=(const file_streambuf&) = delete;

    file_streambuf(const std::string& filename)
        : file_(file_open(filename))
    {
    __builtin_trap() /* STUB: not implemented */;
}

protected:
    virtual
    int_type
    underflow()
    {
    __builtin_trap() /* STUB: not implemented */;
}

private:
    FILE*
    file_open(const std::string& filename)
    {
    __builtin_trap() /* STUB: not implemented */;
}
};

#endif  // !USE_OS_TZDB

namespace date
{
// +---------------------+
// | Begin Configuration |
// +---------------------+

using namespace detail;

#if !USE_OS_TZDB

static
std::string&
access_install()
{
    __builtin_trap() /* STUB: not implemented */;
}

void
set_install(const std::string& install)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
const std::string&
get_install()
{
    __builtin_trap() /* STUB: not implemented */;
}

#if HAS_REMOTE_API
static
std::string
get_download_gz_file(const std::string& version)
{
    __builtin_trap() /* STUB: not implemented */;
}
#endif  // HAS_REMOTE_API

#endif  // !USE_OS_TZDB

// These can be used to reduce the range of the database to save memory
CONSTDATA auto min_year = date::year::min();
CONSTDATA auto max_year = date::year::max();

CONSTDATA auto min_day = date::January/1;
CONSTDATA auto max_day = date::December/31;

#if USE_OS_TZDB

CONSTCD14 const sys_seconds min_seconds = sys_days(min_year/min_day);

#endif  // USE_OS_TZDB

#ifndef _WIN32

static
std::string
discover_tz_dir()
{
    __builtin_trap() /* STUB: not implemented */;
}

static
const std::string&
get_tz_dir()
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif

// +-------------------+
// | End Configuration |
// +-------------------+

#ifndef _MSC_VER
static_assert(min_year <= max_year, "Configuration error");
#endif

#if !defined(ANDROID) && !defined(__ANDROID__)
static std::unique_ptr<tzdb> init_tzdb();
#endif // !defined(ANDROID) && !defined(__ANDROID__)

tzdb_list::~tzdb_list()
{
    const tzdb* ptr = head_;
    head_ = nullptr;
    while (ptr != nullptr)
    {
        auto next = ptr->next;
        delete ptr;
        ptr = next;
    }
}

tzdb_list::tzdb_list(tzdb_list&& x) NOEXCEPT
   : head_{x.head_.exchange(nullptr)}
{
}

void
tzdb_list::push_front(tzdb* tzdb) NOEXCEPT
{
    __builtin_trap() /* STUB: not implemented */;
}

tzdb_list::const_iterator
tzdb_list::erase_after(const_iterator p) NOEXCEPT
{
    __builtin_trap() /* STUB: not implemented */;
}

struct tzdb_list::undocumented_helper
{
    static void push_front(tzdb_list& db_list, tzdb* tzdb) NOEXCEPT
    {
    __builtin_trap() /* STUB: not implemented */;
}
};

static
tzdb_list
create_tzdb()
{
    __builtin_trap() /* STUB: not implemented */;
}

tzdb_list&
get_tzdb_list()
{
    __builtin_trap() /* STUB: not implemented */;
}

#if !defined(ANDROID) && !defined(__ANDROID__)
inline
static
char
tolower(char c)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
static
void
tolower(std::string& s)
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
static
std::string
get_alpha_word(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}
#endif // !defined(ANDROID) && !defined(__ANDROID__)

inline
static
bool
is_prefix_of(std::string const& key, std::string const& value)
{
    __builtin_trap() /* STUB: not implemented */;
}

#if !defined(ANDROID) && !defined(__ANDROID__)
static
unsigned
parse_month(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}
#endif // !defined(ANDROID) && !defined(__ANDROID__)

#if !USE_OS_TZDB

#ifdef _WIN32

static
void
sort_zone_mappings(std::vector<date::detail::timezone_mapping>& mappings)
{
    std::sort(mappings.begin(), mappings.end(),
        [](const date::detail::timezone_mapping& lhs,
           const date::detail::timezone_mapping& rhs)->bool
    {
        auto other_result = lhs.other.compare(rhs.other);
        if (other_result < 0)
            return true;
        else if (other_result == 0)
        {
            auto territory_result = lhs.territory.compare(rhs.territory);
            if (territory_result < 0)
                return true;
            else if (territory_result == 0)
            {
                if (lhs.type < rhs.type)
                    return true;
            }
        }
        return false;
    });
}

static
bool
native_to_standard_timezone_name(const std::string& native_tz_name,
                                 std::string& standard_tz_name)
{
    // TOOD! Need be a case insensitive compare?
    if (native_tz_name == "UTC")
    {
        standard_tz_name = "Etc/UTC";
        return true;
    }
    standard_tz_name.clear();
    // TODO! we can improve on linear search.
    const auto& mappings = date::get_tzdb().mappings;
    for (const auto& tzm : mappings)
    {
        if (tzm.other == native_tz_name)
        {
            standard_tz_name = tzm.type;
            return true;
        }
    }
    return false;
}

// Parse this XML file:
// https://raw.githubusercontent.com/unicode-org/cldr/master/common/supplemental/windowsZones.xml
// The parsing method is designed to be simple and quick. It is not overly
// forgiving of change but it should diagnose basic format issues.
// See timezone_mapping structure for more info.
static
std::vector<detail::timezone_mapping>
load_timezone_mappings_from_xml_file(const std::string& input_path)
{
    std::size_t line_num = 0;
    std::vector<detail::timezone_mapping> mappings;
    std::string line;

    file_streambuf ibuf(input_path);
    std::istream is(&ibuf);

    auto error = [&input_path, &line_num](const char* info)
    {
        std::string msg = "Error loading time zone mapping file \"";
        msg += input_path;
        msg += "\" at line ";
        msg += std::to_string(line_num);
        msg += ": ";
        msg += info;
        throw std::runtime_error(msg);
    };
    // [optional space]a="b"
    auto read_attribute = [&line, &error]
                          (const char* name, std::string& value, std::size_t startPos)
                          ->std::size_t
    {
        value.clear();
        // Skip leading space before attribute name.
        std::size_t spos = line.find_first_not_of(' ', startPos);
        if (spos == std::string::npos)
            spos = startPos;
        // Assume everything up to next = is the attribute name
        // and that an = will always delimit that.
        std::size_t epos = line.find('=', spos);
        if (epos == std::string::npos)
            error("Expected \'=\' right after attribute name.");
        std::size_t name_len = epos - spos;
        // Expect the name we find matches the name we expect.
        if (line.compare(spos, name_len, name) != 0)
        {
            std::string msg;
            msg = "Expected attribute name \'";
            msg += name;
            msg += "\' around position ";
            msg += std::to_string(spos);
            msg += " but found something else.";
            error(msg.c_str());
        }
        ++epos; // Skip the '=' that is after the attribute name.
        spos = epos;
        if (spos < line.length() && line[spos] == '\"')
            ++spos; // Skip the quote that is before the attribute value.
        else
        {
            std::string msg = "Expected '\"' to begin value of attribute \'";
            msg += name;
            msg += "\'.";
            error(msg.c_str());
        }
        epos = line.find('\"', spos);
        if (epos == std::string::npos)
        {
            std::string msg = "Expected '\"' to end value of attribute \'";
            msg += name;
            msg += "\'.";
            error(msg.c_str());
        }
        // Extract everything in between the quotes. Note no escaping is done.
        std::size_t value_len = epos - spos;
        value.assign(line, spos, value_len);
        ++epos; // Skip the quote that is after the attribute value;
        return epos;
    };

    // Quick but not overly forgiving XML mapping file processing.
    bool mapTimezonesOpenTagFound = false;
    bool mapTimezonesCloseTagFound = false;
    std::size_t mapZonePos = std::string::npos;
    std::size_t mapTimezonesPos = std::string::npos;
    CONSTDATA char mapTimeZonesOpeningTag[] = { "<mapTimezones " };
    CONSTDATA char mapZoneOpeningTag[] = { "<mapZone " };
    CONSTDATA std::size_t mapZoneOpeningTagLen = sizeof(mapZoneOpeningTag) /
                                                 sizeof(mapZoneOpeningTag[0]) - 1;
    while (!mapTimezonesOpenTagFound)
    {
        std::getline(is, line);
        ++line_num;
        if (is.eof())
        {
            // If there is no mapTimezones tag is it an error?
            // Perhaps if there are no mapZone mappings it might be ok for
            // its parent mapTimezones element to be missing?
            // We treat this as an error though on the assumption that if there
            // really are no mappings we should still get a mapTimezones parent
            // element but no mapZone elements inside. Assuming we must
            // find something will hopefully at least catch more drastic formatting
            // changes or errors than if we don't do this and assume nothing found.
            error("Expected a mapTimezones opening tag.");
        }
        mapTimezonesPos = line.find(mapTimeZonesOpeningTag);
        mapTimezonesOpenTagFound = (mapTimezonesPos != std::string::npos);
    }

    // NOTE: We could extract the version info that follows the opening
    // mapTimezones tag and compare that to the version of other data we have.
    // I would have expected them to be kept in synch but testing has shown
    // it typically does not match anyway. So what's the point?
    while (!mapTimezonesCloseTagFound)
    {
        std::ws(is);
        std::getline(is, line);
        ++line_num;
        if (is.eof())
            error("Expected a mapTimezones closing tag.");
        if (line.empty())
            continue;
        mapZonePos = line.find(mapZoneOpeningTag);
        if (mapZonePos != std::string::npos)
        {
            mapZonePos += mapZoneOpeningTagLen;
            detail::timezone_mapping zm{};
            std::size_t pos = read_attribute("other", zm.other, mapZonePos);
            pos = read_attribute("territory", zm.territory, pos);
            read_attribute("type", zm.type, pos);
            mappings.push_back(std::move(zm));

            continue;
        }
        mapTimezonesPos = line.find("</mapTimezones>");
        mapTimezonesCloseTagFound = (mapTimezonesPos != std::string::npos);
        if (!mapTimezonesCloseTagFound)
        {
            std::size_t commentPos = line.find("<!--");
            if (commentPos == std::string::npos)
                error("Unexpected mapping record found. A xml mapZone or comment "
                      "attribute or mapTimezones closing tag was expected.");
        }
    }

    return mappings;
}

#endif  // _WIN32

// Parsing helpers

static
unsigned
parse_dow(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::chrono::seconds
parse_unsigned_time(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::chrono::seconds
parse_signed_time(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}

// MonthDayTime

detail::MonthDayTime::MonthDayTime(local_seconds tp, tz timezone)
    : zone_(timezone)
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::MonthDayTime::MonthDayTime(const date::month_day& md, tz timezone)
    : zone_(timezone)
{
    __builtin_trap() /* STUB: not implemented */;
}

date::day
detail::MonthDayTime::day() const
{
    __builtin_trap() /* STUB: not implemented */;
}

date::month
detail::MonthDayTime::month() const
{
    __builtin_trap() /* STUB: not implemented */;
}

int
detail::MonthDayTime::compare(date::year y, const MonthDayTime& x, date::year yx,
                      std::chrono::seconds offset, std::chrono::minutes prev_save) const
{
    __builtin_trap() /* STUB: not implemented */;
}

sys_seconds
detail::MonthDayTime::to_sys(date::year y, std::chrono::seconds offset,
                     std::chrono::seconds save) const
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::MonthDayTime::U&
detail::MonthDayTime::U::operator=(const date::month_day& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::MonthDayTime::U&
detail::MonthDayTime::U::operator=(const date::month_weekday_last& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::MonthDayTime::U&
detail::MonthDayTime::U::operator=(const pair& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

date::sys_days
detail::MonthDayTime::to_sys_days(date::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

sys_seconds
detail::MonthDayTime::to_time_point(date::year y) const
{
    __builtin_trap() /* STUB: not implemented */;
}

void
detail::MonthDayTime::canonicalize(date::year y)
{
    __builtin_trap() /* STUB: not implemented */;
}

std::istream&
detail::operator>>(std::istream& is, MonthDayTime& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

std::ostream&
detail::operator<<(std::ostream& os, const MonthDayTime& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

// Rule

detail::Rule::Rule(const std::string& s)
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::Rule::Rule(const Rule& r, date::year starting_year, date::year ending_year)
    : name_(r.name_)
    , starting_year_(starting_year)
    , ending_year_(ending_year)
    , starting_at_(r.starting_at_)
    , save_(r.save_)
    , abbrev_(r.abbrev_)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator==(const Rule& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator<(const Rule& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator==(const Rule& x, const date::year& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator<(const Rule& x, const date::year& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator==(const date::year& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator<(const date::year& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator==(const Rule& x, const std::string& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator<(const Rule& x, const std::string& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator==(const std::string& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
detail::operator<(const std::string& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

std::ostream&
detail::operator<<(std::ostream& os, const Rule& r)
{
    __builtin_trap() /* STUB: not implemented */;
}

date::day
detail::Rule::day() const
{
    __builtin_trap() /* STUB: not implemented */;
}

date::month
detail::Rule::month() const
{
    __builtin_trap() /* STUB: not implemented */;
}

struct find_rule_by_name
{
    bool operator()(const Rule& x, const std::string& nm) const
    {
    __builtin_trap() /* STUB: not implemented */;
}

    bool operator()(const std::string& nm, const Rule& x) const
    {
    __builtin_trap() /* STUB: not implemented */;
}
};

bool
detail::Rule::overlaps(const Rule& x, const Rule& y)
{
    __builtin_trap() /* STUB: not implemented */;
}

void
detail::Rule::split(std::vector<Rule>& rules, std::size_t i, std::size_t k, std::size_t& e)
{
    __builtin_trap() /* STUB: not implemented */;
}

void
detail::Rule::split_overlaps(std::vector<Rule>& rules, std::size_t i, std::size_t& e)
{
    __builtin_trap() /* STUB: not implemented */;
}

void
detail::Rule::split_overlaps(std::vector<Rule>& rules)
{
    __builtin_trap() /* STUB: not implemented */;
}

// Find the rule that comes chronologically before Rule r.  For multi-year rules,
// y specifies which rules in r.  For single year rules, y is assumed to be equal
// to the year specified by r.
// Returns a pointer to the chronologically previous rule, and the year within
// that rule.  If there is no previous rule, returns nullptr and year::min().
// Preconditions:
//     r->starting_year() <= y && y <= r->ending_year()
static
std::pair<const Rule*, date::year>
find_previous_rule(const Rule* r, date::year y)
{
    __builtin_trap() /* STUB: not implemented */;
}

// Find the rule that comes chronologically after Rule r.  For multi-year rules,
// y specifies which rules in r.  For single year rules, y is assumed to be equal
// to the year specified by r.
// Returns a pointer to the chronologically next rule, and the year within
// that rule.  If there is no next rule, return a pointer to a defaulted rule
// and y+1.
// Preconditions:
//     first <= r && r < last && r->starting_year() <= y && y <= r->ending_year()
//     [first, last) all have the same name
static
std::pair<const Rule*, date::year>
find_next_rule(const Rule* first_rule, const Rule* last_rule, const Rule* r, date::year y)
{
    __builtin_trap() /* STUB: not implemented */;
}

// Find the rule that comes chronologically after Rule r.  For multi-year rules,
// y specifies which rules in r.  For single year rules, y is assumed to be equal
// to the year specified by r.
// Returns a pointer to the chronologically next rule, and the year within
// that rule.  If there is no next rule, return nullptr and year::max().
// Preconditions:
//     r->starting_year() <= y && y <= r->ending_year()
static
std::pair<const Rule*, date::year>
find_next_rule(const Rule* r, date::year y)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
const Rule*
find_first_std_rule(const std::pair<const Rule*, const Rule*>& eqr)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::pair<const Rule*, date::year>
find_rule_for_zone(const std::pair<const Rule*, const Rule*>& eqr,
                   const date::year& y, const std::chrono::seconds& offset,
                   const MonthDayTime& mdt)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::pair<const Rule*, date::year>
find_rule_for_zone(const std::pair<const Rule*, const Rule*>& eqr,
                   const sys_seconds& tp_utc,
                   const local_seconds& tp_std,
                   const local_seconds& tp_loc)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
sys_info
find_rule(const std::pair<const Rule*, date::year>& first_rule,
          const std::pair<const Rule*, date::year>& last_rule,
          const date::year& y, const std::chrono::seconds& offset,
          const MonthDayTime& mdt, const std::chrono::minutes& initial_save,
          const std::string& initial_abbrev)
{
    __builtin_trap() /* STUB: not implemented */;
}

// zonelet

detail::zonelet::~zonelet()
{
#if !defined(_MSC_VER) || (_MSC_VER >= 1900)
    using minutes = std::chrono::minutes;
    using string = std::string;
    if (tag_ == has_save)
        u.save_.~minutes();
    else
        u.rule_.~string();
#endif
}

detail::zonelet::zonelet()
{
    __builtin_trap() /* STUB: not implemented */;
}

detail::zonelet::zonelet(const zonelet& i)
    : gmtoff_(i.gmtoff_)
    , tag_(i.tag_)
    , format_(i.format_)
    , until_year_(i.until_year_)
    , until_date_(i.until_date_)
    , until_utc_(i.until_utc_)
    , until_std_(i.until_std_)
    , until_loc_(i.until_loc_)
    , initial_save_(i.initial_save_)
    , initial_abbrev_(i.initial_abbrev_)
    , first_rule_(i.first_rule_)
    , last_rule_(i.last_rule_)
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // !USE_OS_TZDB

// time_zone

#if USE_OS_TZDB

time_zone::time_zone(const std::string& s, detail::undocumented)
    : name_(s)
    , adjusted_(new std::once_flag{})
{
}

enum class endian
{
    native = __BYTE_ORDER__,
    little = __ORDER_LITTLE_ENDIAN__,
    big    = __ORDER_BIG_ENDIAN__
};

static
inline
std::uint32_t
reverse_bytes(std::uint32_t i)
{
    return
        (i & 0xff000000u) >> 24 |
        (i & 0x00ff0000u) >> 8 |
        (i & 0x0000ff00u) << 8 |
        (i & 0x000000ffu) << 24;
}

static
inline
std::uint64_t
reverse_bytes(std::uint64_t i)
{
    return
        (i & 0xff00000000000000ull) >> 56 |
        (i & 0x00ff000000000000ull) >> 40 |
        (i & 0x0000ff0000000000ull) >> 24 |
        (i & 0x000000ff00000000ull) >> 8 |
        (i & 0x00000000ff000000ull) << 8 |
        (i & 0x0000000000ff0000ull) << 24 |
        (i & 0x000000000000ff00ull) << 40 |
        (i & 0x00000000000000ffull) << 56;
}

template <class T>
static
inline
void
maybe_reverse_bytes(T&, std::false_type)
{
}

static
inline
void
maybe_reverse_bytes(std::int32_t& t, std::true_type)
{
    t = static_cast<std::int32_t>(reverse_bytes(static_cast<std::uint32_t>(t)));
}

static
inline
void
maybe_reverse_bytes(std::int64_t& t, std::true_type)
{
    t = static_cast<std::int64_t>(reverse_bytes(static_cast<std::uint64_t>(t)));
}

template <class T>
static
inline
void
maybe_reverse_bytes(T& t)
{
    maybe_reverse_bytes(t, std::integral_constant<bool,
                                                  endian::native == endian::little>{});
}

static
void
load_header(std::istream& inf)
{
    // Read TZif
    auto t = inf.get();
    auto z = inf.get();
    auto i = inf.get();
    auto f = inf.get();
#ifndef NDEBUG
    assert(t == 'T');
    assert(z == 'Z');
    assert(i == 'i');
    assert(f == 'f');
#else
    (void)t;
    (void)z;
    (void)i;
    (void)f;
#endif
}

static
unsigned char
load_version(std::istream& inf)
{
    // Read version
    auto v = inf.get();
    assert(v != EOF);
    return static_cast<unsigned char>(v);
}

static
void
skip_reserve(std::istream& inf)
{
    inf.ignore(15);
}

static
void
load_counts(std::istream& inf,
            std::int32_t& tzh_ttisgmtcnt, std::int32_t& tzh_ttisstdcnt,
            std::int32_t& tzh_leapcnt,    std::int32_t& tzh_timecnt,
            std::int32_t& tzh_typecnt,    std::int32_t& tzh_charcnt)
{
    // Read counts;
    inf.read(reinterpret_cast<char*>(&tzh_ttisgmtcnt), 4);
    maybe_reverse_bytes(tzh_ttisgmtcnt);
    inf.read(reinterpret_cast<char*>(&tzh_ttisstdcnt), 4);
    maybe_reverse_bytes(tzh_ttisstdcnt);
    inf.read(reinterpret_cast<char*>(&tzh_leapcnt), 4);
    maybe_reverse_bytes(tzh_leapcnt);
    inf.read(reinterpret_cast<char*>(&tzh_timecnt), 4);
    maybe_reverse_bytes(tzh_timecnt);
    inf.read(reinterpret_cast<char*>(&tzh_typecnt), 4);
    maybe_reverse_bytes(tzh_typecnt);
    inf.read(reinterpret_cast<char*>(&tzh_charcnt), 4);
    maybe_reverse_bytes(tzh_charcnt);
}

template <class TimeType>
static
std::vector<detail::transition>
load_transitions(std::istream& inf, std::int32_t tzh_timecnt)
{
    // Read transitions
    using namespace std::chrono;
    std::vector<detail::transition> transitions;
    transitions.reserve(static_cast<unsigned>(tzh_timecnt));
    for (std::int32_t i = 0; i < tzh_timecnt; ++i)
    {
        TimeType t;
        inf.read(reinterpret_cast<char*>(&t), sizeof(t));
        maybe_reverse_bytes(t);
        transitions.emplace_back(sys_seconds{seconds{t}});
        if (transitions.back().timepoint < min_seconds)
            transitions.back().timepoint = min_seconds;
    }
    return transitions;
}

static
std::vector<std::uint8_t>
load_indices(std::istream& inf, std::int32_t tzh_timecnt)
{
    // Read indices
    std::vector<std::uint8_t> indices;
    indices.reserve(static_cast<unsigned>(tzh_timecnt));
    for (std::int32_t i = 0; i < tzh_timecnt; ++i)
    {
        std::uint8_t t;
        inf.read(reinterpret_cast<char*>(&t), sizeof(t));
        indices.emplace_back(t);
    }
    return indices;
}

static
std::vector<ttinfo>
load_ttinfo(std::istream& inf, std::int32_t tzh_typecnt)
{
    // Read ttinfo
    std::vector<ttinfo> ttinfos;
    ttinfos.reserve(static_cast<unsigned>(tzh_typecnt));
    for (std::int32_t i = 0; i < tzh_typecnt; ++i)
    {
        ttinfo t;
        inf.read(reinterpret_cast<char*>(&t), 6);
        maybe_reverse_bytes(t.tt_gmtoff);
        ttinfos.emplace_back(t);
    }
    return ttinfos;
}

static
std::string
load_abbreviations(std::istream& inf, std::int32_t tzh_charcnt)
{
    // Read abbreviations
    std::string abbrev;
    abbrev.resize(static_cast<unsigned>(tzh_charcnt), '\0');
    inf.read(&abbrev[0], tzh_charcnt);
    return abbrev;
}

#if !MISSING_LEAP_SECONDS

template <class TimeType>
static
std::vector<leap_second>
load_leaps(std::istream& inf, std::int32_t tzh_leapcnt)
{
    // Read tzh_leapcnt pairs
    using namespace std::chrono;
    std::vector<leap_second> leap_seconds;
    leap_seconds.reserve(static_cast<std::size_t>(tzh_leapcnt));
    for (std::int32_t i = 0; i < tzh_leapcnt; ++i)
    {
        TimeType     t0;
        std::int32_t t1;
        inf.read(reinterpret_cast<char*>(&t0), sizeof(t0));
        inf.read(reinterpret_cast<char*>(&t1), sizeof(t1));
        maybe_reverse_bytes(t0);
        maybe_reverse_bytes(t1);
        leap_seconds.emplace_back(sys_seconds{seconds{t0 - (t1-1)}},
                                  detail::undocumented{});
    }
    return leap_seconds;
}

template <class TimeType>
static
std::vector<leap_second>
load_leap_data(std::istream& inf,
               std::int32_t tzh_leapcnt, std::int32_t tzh_timecnt,
               std::int32_t tzh_typecnt, std::int32_t tzh_charcnt)
{
    inf.ignore(tzh_timecnt*static_cast<std::int32_t>(sizeof(TimeType)) + tzh_timecnt +
               tzh_typecnt*6 + tzh_charcnt);
    return load_leaps<TimeType>(inf, tzh_leapcnt);
}

static
std::vector<leap_second>
load_just_leaps(std::istream& inf)
{
    // Read tzh_leapcnt pairs
    using namespace std::chrono;
    load_header(inf);
    auto v = load_version(inf);
    std::int32_t tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                 tzh_timecnt,    tzh_typecnt,    tzh_charcnt;
    skip_reserve(inf);
    load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                     tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
    if (v == 0)
        return load_leap_data<int32_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt,
                                       tzh_charcnt);
#if !defined(NDEBUG)
    inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
               tzh_ttisstdcnt + tzh_ttisgmtcnt);
    load_header(inf);
    auto v2 = load_version(inf);
    assert(v == v2);
    skip_reserve(inf);
#else  // defined(NDEBUG)
    inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
               tzh_ttisstdcnt + tzh_ttisgmtcnt + (4+1+15));
#endif  // defined(NDEBUG)
    load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                     tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
    return load_leap_data<int64_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt,
                                   tzh_charcnt);
}

#endif  // !MISSING_LEAP_SECONDS

template <class TimeType>
void
time_zone::load_data(std::istream& inf,
                     std::int32_t tzh_leapcnt, std::int32_t tzh_timecnt,
                     std::int32_t tzh_typecnt, std::int32_t tzh_charcnt)
{
    using namespace std::chrono;
    transitions_ = load_transitions<TimeType>(inf, tzh_timecnt);
    auto indices = load_indices(inf, tzh_timecnt);
    auto infos = load_ttinfo(inf, tzh_typecnt);
    auto abbrev = load_abbreviations(inf, tzh_charcnt);
#if !MISSING_LEAP_SECONDS
    auto& leap_seconds = get_tzdb_list().front().leap_seconds;
    if (leap_seconds.empty() && tzh_leapcnt > 0)
        leap_seconds = load_leaps<TimeType>(inf, tzh_leapcnt);
#endif
    ttinfos_.reserve(infos.size());
    for (auto& info : infos)
    {
        ttinfos_.push_back({seconds{info.tt_gmtoff},
                            abbrev.c_str() + info.tt_abbrind,
                            info.tt_isdst != 0});
    }
    auto i = 0u;
    if (transitions_.empty() || transitions_.front().timepoint != min_seconds)
    {
        transitions_.emplace(transitions_.begin(), min_seconds);
        auto tf = std::find_if(ttinfos_.begin(), ttinfos_.end(),
                               [](const expanded_ttinfo& ti)
                                   {return ti.is_dst == 0;});
        if (tf == ttinfos_.end())
            tf = ttinfos_.begin();
        transitions_[i].info = &*tf;
        ++i;
    }
    for (auto j = 0u; i < transitions_.size(); ++i, ++j)
        transitions_[i].info = ttinfos_.data() + indices[j];
}

void
time_zone::init_impl()
{
#if defined(ANDROID) || defined(__ANDROID__)
    return;
#endif // defined(ANDROID) || defined(__ANDROID__)
    using namespace std;
    using namespace std::chrono;
    auto name = get_tz_dir() + ('/' + name_);
    std::ifstream inf(name);
    if (!inf.is_open())
        throw std::runtime_error{"Unable to open " + name};
    inf.exceptions(std::ios::failbit | std::ios::badbit);
    load_header(inf);
    auto v = load_version(inf);
    std::int32_t tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                 tzh_timecnt,    tzh_typecnt,    tzh_charcnt;
    skip_reserve(inf);
    load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                     tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
    if (v == 0)
    {
        load_data<int32_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt, tzh_charcnt);
    }
    else
    {
#if !defined(NDEBUG)
        inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
                   tzh_ttisstdcnt + tzh_ttisgmtcnt);
        load_header(inf);
        auto v2 = load_version(inf);
        assert(v == v2);
        skip_reserve(inf);
#else  // defined(NDEBUG)
        inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
                   tzh_ttisstdcnt + tzh_ttisgmtcnt + (4+1+15));
#endif  // defined(NDEBUG)
        load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                         tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
        load_data<int64_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt, tzh_charcnt);
    }
#if !MISSING_LEAP_SECONDS
    if (tzh_leapcnt > 0)
    {
        auto& leap_seconds = get_tzdb_list().front().leap_seconds;
        auto itr = leap_seconds.begin();
        auto l = itr->date();
        seconds leap_count{0};
        for (auto t = std::upper_bound(transitions_.begin(), transitions_.end(), l,
                                       [](const sys_seconds& x, const transition& ct)
                                       {
                                           return x < ct.timepoint;
                                       });
                  t != transitions_.end(); ++t)
        {
            while (t->timepoint >= l)
            {
                ++leap_count;
                if (++itr == leap_seconds.end())
                    l = sys_days(max_year/max_day);
                else
                    l = itr->date() + leap_count;
            }
            t->timepoint -= leap_count;
        }
    }
#endif  // !MISSING_LEAP_SECONDS
    auto b = transitions_.begin();
    auto i = transitions_.end();
    if (i != b)
    {
        for (--i; i != b; --i)
        {
            if (i->info->offset == i[-1].info->offset &&
                i->info->abbrev == i[-1].info->abbrev &&
                i->info->is_dst == i[-1].info->is_dst)
                i = transitions_.erase(i);
        }
    }
}

void
time_zone::init() const
{
    std::call_once(*adjusted_, [this]() {const_cast<time_zone*>(this)->init_impl();});
}

sys_info
time_zone::load_sys_info(std::vector<detail::transition>::const_iterator i) const
{
    using namespace std::chrono;
    assert(!transitions_.empty());
    sys_info r;
    if (i != transitions_.begin())
    {
        r.begin = i[-1].timepoint;
        r.end = i != transitions_.end() ? i->timepoint :
                                          sys_seconds(sys_days(year::max()/max_day));
        r.offset = i[-1].info->offset;
        r.save = i[-1].info->is_dst ? minutes{1} : minutes{0};
        r.abbrev = i[-1].info->abbrev;
    }
    else
    {
        r.begin = sys_days(year::min()/min_day);
        r.end = i+1 != transitions_.end() ? i[1].timepoint :
                                          sys_seconds(sys_days(year::max()/max_day));
        r.offset = i[0].info->offset;
        r.save = i[0].info->is_dst ? minutes{1} : minutes{0};
        r.abbrev = i[0].info->abbrev;
    }
    return r;
}

sys_info
time_zone::get_info_impl(sys_seconds tp) const
{
    using namespace std;
    init();
    return load_sys_info(upper_bound(transitions_.begin(), transitions_.end(), tp,
                                     [](const sys_seconds& x, const transition& t)
                                     {
                                         return x < t.timepoint;
                                     }));
}

local_info
time_zone::get_info_impl(local_seconds tp) const
{
    using namespace std::chrono;
    init();
    local_info i{};
    i.result = local_info::unique;
    auto tr = upper_bound(transitions_.begin(), transitions_.end(), tp,
                          [](const local_seconds& x, const transition& t)
                          {
                              return sys_seconds{x.time_since_epoch()} -
                                                         t.info->offset < t.timepoint;
                          });
    i.first = load_sys_info(tr);
    auto tps = sys_seconds{(tp - i.first.offset).time_since_epoch()};
    if (tps < i.first.begin + days{1} && tr != transitions_.begin())
    {
        i.second = load_sys_info(--tr);
        tps = sys_seconds{(tp - i.second.offset).time_since_epoch()};
        if (tps < i.second.end && i.first.end != i.second.end)
        {
           i.result = local_info::ambiguous;
           std::swap(i.first, i.second);
        }
        else
        {
            i.second = {};
        }
    }
    else if (tps >= i.first.end && tr != transitions_.end())
    {
        i.second = load_sys_info(++tr);
        tps = sys_seconds{(tp - i.second.offset).time_since_epoch()};
        if (tps < i.second.begin)
            i.result = local_info::nonexistent;
        else
            i.second = {};
    }
    return i;
}

#if defined(ANDROID) || defined(__ANDROID__)
void
time_zone::parse_from_android_tzdata(std::ifstream& inf, const std::size_t off)
{
    using namespace std;
    using namespace std::chrono;
    if (!inf.is_open())
        throw std::runtime_error{"Unable to open tzdata"};
    std::size_t restorepos = inf.tellg();
    inf.seekg(off, inf.beg);
    load_header(inf);
    auto v = load_version(inf);
    std::int32_t tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                 tzh_timecnt,    tzh_typecnt,    tzh_charcnt;
    skip_reserve(inf);
    load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                     tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
    if (v == 0)
    {
        load_data<int32_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt, tzh_charcnt);
    }
    else
    {
#if !defined(NDEBUG)
        inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
                   tzh_ttisstdcnt + tzh_ttisgmtcnt);
        load_header(inf);
        auto v2 = load_version(inf);
        assert(v == v2);
        skip_reserve(inf);
#else  // defined(NDEBUG)
        inf.ignore((4+1)*tzh_timecnt + 6*tzh_typecnt + tzh_charcnt + 8*tzh_leapcnt +
                   tzh_ttisstdcnt + tzh_ttisgmtcnt + (4+1+15));
#endif  // defined(NDEBUG)
        load_counts(inf, tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt,
                         tzh_timecnt,    tzh_typecnt,    tzh_charcnt);
        load_data<int64_t>(inf, tzh_leapcnt, tzh_timecnt, tzh_typecnt, tzh_charcnt);
    }
#if !MISSING_LEAP_SECONDS
    if (tzh_leapcnt > 0)
    {
        auto& leap_seconds = get_tzdb_list().front().leap_seconds;
        auto itr = leap_seconds.begin();
        auto l = itr->date();
        seconds leap_count{0};
        for (auto t = std::upper_bound(transitions_.begin(), transitions_.end(), l,
                                       [](const sys_seconds& x, const transition& ct)
                                       {
                                           return x < ct.timepoint;
                                       });
                  t != transitions_.end(); ++t)
        {
            while (t->timepoint >= l)
            {
                ++leap_count;
                if (++itr == leap_seconds.end())
                    l = sys_days(max_year/max_day);
                else
                    l = itr->date() + leap_count;
            }
            t->timepoint -= leap_count;
        }
    }
#endif  // !MISSING_LEAP_SECONDS
    auto b = transitions_.begin();
    auto i = transitions_.end();
    if (i != b)
    {
        for (--i; i != b; --i)
        {
            if (i->info->offset == i[-1].info->offset &&
                i->info->abbrev == i[-1].info->abbrev &&
                i->info->is_dst == i[-1].info->is_dst)
                i = transitions_.erase(i);
        }
    }
    inf.seekg(restorepos, inf.beg);
}
#endif // defined(ANDROID) || defined(__ANDROID__)

std::ostream&
operator<<(std::ostream& os, const time_zone& z)
{
    using namespace std::chrono;
    z.init();
    os << z.name_ << '\n';
    os << "Initially:           ";
    auto const& t = z.transitions_.front();
    if (t.info->offset >= seconds{0})
        os << '+';
    os << make_time(t.info->offset);
    if (t.info->is_dst > 0)
        os << " daylight ";
    else
        os << " standard ";
    os << t.info->abbrev << '\n';
    for (auto i = std::next(z.transitions_.cbegin()); i < z.transitions_.cend(); ++i)
        os << *i << '\n';
    return os;
}

leap_second::leap_second(const sys_seconds& s, detail::undocumented)
    : date_(s)
{
}

#else  // !USE_OS_TZDB

time_zone::time_zone(const std::string& s, detail::undocumented)
    : adjusted_(new std::once_flag{})
{
    __builtin_trap() /* STUB: not implemented */;
}

sys_info
time_zone::get_info_impl(sys_seconds tp) const
{
    __builtin_trap() /* STUB: not implemented */;
}

local_info
time_zone::get_info_impl(local_seconds tp) const
{
    __builtin_trap() /* STUB: not implemented */;
}

void
time_zone::add(const std::string& s)
{
    __builtin_trap() /* STUB: not implemented */;
}

void
time_zone::parse_info(std::istream& in)
{
    __builtin_trap() /* STUB: not implemented */;
}

void
time_zone::adjust_infos(const std::vector<Rule>& rules)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::string
format_abbrev(std::string format, const std::string& variable, std::chrono::seconds off,
                                                               std::chrono::minutes save)
{
    __builtin_trap() /* STUB: not implemented */;
}

sys_info
time_zone::get_info_impl(sys_seconds tp, int tz_int) const
{
    __builtin_trap() /* STUB: not implemented */;
}

std::ostream&
operator<<(std::ostream& os, const time_zone& z)
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // !USE_OS_TZDB

std::ostream&
operator<<(std::ostream& os, const leap_second& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

#if USE_OS_TZDB

#if !defined(ANDROID) && !defined(__ANDROID__)
static
std::string
get_version()
{
    auto path = get_tz_dir() + std::string("/+VERSION");
    std::ifstream in{path};
    std::string version;
    if (in)
    {
        in >> version;
        return version;
    }
    in.clear();
    in.open(get_tz_dir() + std::string(1, folder_delimiter) + "version");
    if (in)
    {
        in >> version;
        return version;
    }
    version = "unknown";
    return version;
}

static
std::vector<leap_second>
find_read_and_leap_seconds()
{
    std::ifstream in(get_tz_dir() + std::string(1, folder_delimiter) + "leapseconds",
                     std::ios_base::binary);
    if (in)
    {
        std::vector<leap_second> leap_seconds;
        std::string line;
        while (in)
        {
            std::getline(in, line);
            if (!line.empty() && line[0] != '#')
            {
                std::istringstream iss(line);
                iss.exceptions(std::ios::failbit | std::ios::badbit);
                std::string word;
                iss >> word;
                tolower(word);
                if (is_prefix_of(word, "leap"))
                {
                    int y, m, d;
                    iss >> y;
                    m = static_cast<int>(parse_month(iss));
                    iss >> d;
                    leap_seconds.push_back(leap_second(sys_days{year{y}/m/d} + days{1},
                                                                 detail::undocumented{}));
                }
                else
                {
                    std::cerr << line << '\n';
                }
            }
        }
        return leap_seconds;
    }
    in.clear();
    in.open(get_tz_dir() + std::string(1, folder_delimiter) + "leap-seconds.list",
                     std::ios_base::binary);
    if (in)
    {
        std::vector<leap_second> leap_seconds;
        std::string line;
        const auto offset = sys_days{1970_y/1/1}-sys_days{1900_y/1/1};
        while (in)
        {
            std::getline(in, line);
            if (!line.empty() && line[0] != '#')
            {
                std::istringstream iss(line);
                iss.exceptions(std::ios::failbit | std::ios::badbit);
                using seconds = std::chrono::seconds;
                seconds::rep s;
                iss >> s;
                if (s == 2272060800)
                    continue;
                leap_seconds.push_back(leap_second(sys_seconds{seconds{s}} - offset,
                                                                 detail::undocumented{}));
            }
        }
        return leap_seconds;
    }
#if !MISSING_LEAP_SECONDS
    in.clear();
    in.open(get_tz_dir() + std::string(1, folder_delimiter) + "right/UTC",
                     std::ios_base::binary);
    if (in)
    {
        return load_just_leaps(in);
    }
    in.clear();
    in.open(get_tz_dir() + std::string(1, folder_delimiter) + "UTC",
                     std::ios_base::binary);
    if (in)
    {
        return load_just_leaps(in);
    }
#endif
    return {};
}
#endif // !defined(ANDROID) && !defined(__ANDROID__)

static
std::unique_ptr<tzdb>
init_tzdb()
{
    std::unique_ptr<tzdb> db(new tzdb);

#if defined(ANDROID) || defined(__ANDROID__)
    auto path = get_tz_dir() + std::string("/tzdata");
    std::ifstream in{path};
    if (!in)
        throw std::runtime_error("Can not open " + path);
    bionic_tzdata_header_t hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(bionic_tzdata_header_t));
    if (!is_prefix_of(hdr.tzdata_version, "tzdata") || hdr.tzdata_version[11] != 0)
        throw std::runtime_error("Malformed tzdata - invalid magic!");
    maybe_reverse_bytes(hdr.index_offset);
    maybe_reverse_bytes(hdr.data_offset);
    maybe_reverse_bytes(hdr.final_offset);
    if (hdr.index_offset > hdr.data_offset)
        throw std::runtime_error("Malformed tzdata - hdr.index_offset > hdr.data_offset!");
    const size_t index_size = hdr.data_offset - hdr.index_offset;
    if ((index_size % sizeof(index_entry_t)) != 0)
        throw std::runtime_error("Malformed tzdata - index size malformed!");
    //Iterate through zone index
    index_entry_t index_entry{};
    for (size_t idx = 0; idx < index_size; idx += sizeof(index_entry_t)) {
        in.read(reinterpret_cast<char*>(&index_entry), sizeof(index_entry_t));
        maybe_reverse_bytes(index_entry.start);
        maybe_reverse_bytes(index_entry.length);
        time_zone timezone{std::string(index_entry.buf),
                           detail::undocumented{}};
        timezone.parse_from_android_tzdata(in, hdr.data_offset + index_entry.start);
        db->zones.emplace_back(std::move(timezone));
    }
    db->zones.shrink_to_fit();
    std::sort(db->zones.begin(), db->zones.end());
    db->version = std::string(hdr.tzdata_version).replace(0, 6, "");
#else
    //Iterate through folders
    std::queue<std::string> subfolders;
    subfolders.emplace(get_tz_dir());
    struct dirent* d;
    struct stat s;
    while (!subfolders.empty())
    {
        auto dirname = std::move(subfolders.front());
        subfolders.pop();
        auto dir = opendir(dirname.c_str());
        if (!dir)
            continue;
        while ((d = readdir(dir)) != nullptr)
        {
            // Ignore these files:
            if (d->d_name[0]                      == '.'    || // curdir, prevdir, hidden
                memcmp(d->d_name, "posix", 5)     == 0      || // starts with posix
                strcmp(d->d_name, "Factory")      == 0      ||
                strcmp(d->d_name, "iso3166.tab")  == 0      ||
                strcmp(d->d_name, "right")        == 0      ||
                strcmp(d->d_name, "+VERSION")     == 0      ||
                strcmp(d->d_name, "version")      == 0      ||
                strcmp(d->d_name, "zone.tab")     == 0      ||
                strcmp(d->d_name, "zone1970.tab") == 0      ||
                strcmp(d->d_name, "zonenow.tab")  == 0      ||
                strcmp(d->d_name, "tzdata.zi")    == 0      ||
                strcmp(d->d_name, "leapseconds")  == 0      ||
                strcmp(d->d_name, "leap-seconds.list") == 0   )
                continue;
            auto subname = dirname + folder_delimiter + d->d_name;
            if(stat(subname.c_str(), &s) == 0)
            {
                if(S_ISDIR(s.st_mode))
                {
                    if(!S_ISLNK(s.st_mode))
                    {
                        subfolders.push(subname);
                    }
                }
                else
                {
                    db->zones.emplace_back(subname.substr(get_tz_dir().size()+1),
                                           detail::undocumented{});
                }
            }
        }
        closedir(dir);
    }
    db->zones.shrink_to_fit();
    std::sort(db->zones.begin(), db->zones.end());
    db->leap_seconds = find_read_and_leap_seconds();
    db->version = get_version();
#endif // defined(ANDROID) || defined(__ANDROID__)
    return db;
}

#else  // !USE_OS_TZDB

// time_zone_link

time_zone_link::time_zone_link(const std::string& s)
{
    __builtin_trap() /* STUB: not implemented */;
}

std::ostream&
operator<<(std::ostream& os, const time_zone_link& x)
{
    __builtin_trap() /* STUB: not implemented */;
}

// leap_second

leap_second::leap_second(const std::string& s, detail::undocumented)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
bool
file_exists(const std::string& filename)
{
    __builtin_trap() /* STUB: not implemented */;
}

#if HAS_REMOTE_API

// CURL tools

namespace
{

struct curl_global_init_and_cleanup
{
    ~curl_global_init_and_cleanup()
    {
        ::curl_global_cleanup();
    }
    curl_global_init_and_cleanup()
    {
    __builtin_trap() /* STUB: not implemented */;
}
    curl_global_init_and_cleanup(curl_global_init_and_cleanup const&) = delete;
    curl_global_init_and_cleanup& operator=(curl_global_init_and_cleanup const&) = delete;
};

struct curl_deleter
{
    void operator()(CURL* p) const
    {
    __builtin_trap() /* STUB: not implemented */;
}
};

}  // unnamed namespace

static
std::unique_ptr<CURL, curl_deleter>
curl_init()
{
    __builtin_trap() /* STUB: not implemented */;
}

static
bool
download_to_string(const std::string& url, std::string& str)
{
    __builtin_trap() /* STUB: not implemented */;
}

namespace
{
    enum class download_file_options { binary, text };
}

static
bool
download_to_file(const std::string& url, const std::string& local_filename,
                 download_file_options opts, char* error_buffer)
{
    __builtin_trap() /* STUB: not implemented */;
}

std::string
remote_version()
{
    __builtin_trap() /* STUB: not implemented */;
}


// TODO! Using system() create a process and a console window.
// This is useful to see what errors may occur but is slow and distracting.
// Consider implementing this functionality more directly, such as
// using _mkdir and CreateProcess etc.
// But use the current means now as matches Unix implementations and while
// in proof of concept / testing phase.
// TODO! Use <filesystem> eventually.
static
bool
remove_folder_and_subfolders(const std::string& folder)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
bool
make_directory(const std::string& folder)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
bool
delete_file(const std::string& file)
{
    __builtin_trap() /* STUB: not implemented */;
}

#  ifdef _WIN32

static
bool
move_file(const std::string& from, const std::string& to)
{
#    if USE_SHELL_API
    std::string cmd = "move \"";
    cmd += from;
    cmd += "\" \"";
    cmd += to;
    cmd += '\"';
    return std::system(cmd.c_str()) == EXIT_SUCCESS;
#    else  // !USE_SHELL_API
    return !!::MoveFile(from.c_str(), to.c_str());
#    endif // !USE_SHELL_API
}

// Usually something like "c:\Program Files".
static
std::string
get_program_folder()
{
    return get_known_folder(FOLDERID_ProgramFiles);
}

// Note folder can and usually does contain spaces.
static
std::string
get_unzip_program()
{
    std::string path;

    // 7-Zip appears to note its location in the registry.
    // If that doesn't work, fall through and take a guess, but it will likely be wrong.
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\7-Zip", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char value_buffer[MAX_PATH + 1]; // fyi 260 at time of writing.
        // in/out parameter. Documentation say that size is a count of bytes not chars.
        DWORD size = sizeof(value_buffer) - sizeof(value_buffer[0]);
        DWORD tzi_type = REG_SZ;
        // Testing shows Path key value is "C:\Program Files\7-Zip\" i.e. always with trailing \.
        bool got_value = (RegQueryValueExA(hKey, "Path", nullptr, &tzi_type,
            reinterpret_cast<LPBYTE>(value_buffer), &size) == ERROR_SUCCESS);
        RegCloseKey(hKey); // Close now incase of throw later.
        if (got_value)
        {
            // Function does not guarantee to null terminate.
            value_buffer[size / sizeof(value_buffer[0])] = '\0';
            path = value_buffer;
            if (!path.empty())
            {
                path += "7z.exe";
                return path;
            }
        }
    }
    path += get_program_folder();
    path += folder_delimiter;
    path += "7-Zip\\7z.exe";
    return path;
}

#    if !USE_SHELL_API
static
int
run_program(const std::string& command)
{
    STARTUPINFO si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Allegedly CreateProcess overwrites the command line. Ugh.
    std::string mutable_command(command);
    if (CreateProcess(nullptr, &mutable_command[0],
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code;
        bool got_exit_code = !!GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        // Not 100% sure about this still active thing is correct,
        // but I'm going with it because I *think* WaitForSingleObject might
        // return in some cases without INFINITE-ly waiting.
        // But why/wouldn't GetExitCodeProcess return false in that case?
        if (got_exit_code && exit_code != STILL_ACTIVE)
            return static_cast<int>(exit_code);
    }
    return EXIT_FAILURE;
}
#    endif // !USE_SHELL_API

static
std::string
get_download_tar_file(const std::string& version)
{
    auto file = get_install();
    file += folder_delimiter;
    file += "tzdata";
    file += version;
    file += ".tar";
    return file;
}

static
bool
extract_gz_file(const std::string& version, const std::string& gz_file,
                const std::string& dest_folder)
{
    auto unzip_prog = get_unzip_program();
    bool unzip_result = false;
    // Use the unzip program to extract the tar file from the archive.

    // Aim to create a string like:
    // "C:\Program Files\7-Zip\7z.exe" x "C:\Users\SomeUser\Downloads\tzdata2016d.tar.gz"
    //     -o"C:\Users\SomeUser\Downloads\tzdata"
    std::string cmd;
    cmd = '\"';
    cmd += unzip_prog;
    cmd += "\" x \"";
    cmd += gz_file;
    cmd += "\" -o\"";
    cmd += dest_folder;
    cmd += '\"';

#    if USE_SHELL_API
    // When using shelling out with std::system() extra quotes are required around the
    // whole command. It's weird but necessary it seems, see:
    // http://stackoverflow.com/q/27975969/576911

    cmd = "\"" + cmd + "\"";
    if (std::system(cmd.c_str()) == EXIT_SUCCESS)
        unzip_result = true;
#    else  // !USE_SHELL_API
    if (run_program(cmd) == EXIT_SUCCESS)
        unzip_result = true;
#    endif // !USE_SHELL_API
    if (unzip_result)
        delete_file(gz_file);

    // Use the unzip program extract the data from the tar file that was
    // just extracted from the archive.
    auto tar_file = get_download_tar_file(version);
    cmd = '\"';
    cmd += unzip_prog;
    cmd += "\" x \"";
    cmd += tar_file;
    cmd += "\" -o\"";
    cmd += get_install();
    cmd += '\"';
#    if USE_SHELL_API
    cmd = "\"" + cmd + "\"";
    if (std::system(cmd.c_str()) == EXIT_SUCCESS)
        unzip_result = true;
#    else  // !USE_SHELL_API
    if (run_program(cmd) == EXIT_SUCCESS)
        unzip_result = true;
#    endif // !USE_SHELL_API

    if (unzip_result)
        delete_file(tar_file);

    return unzip_result;
}

static
std::string
get_download_mapping_file(const std::string& version)
{
    auto file = get_install() + version + "windowsZones.xml";
    return file;
}

#  else  // !_WIN32

#    if !USE_SHELL_API
static
int
run_program(const char* prog, const char*const args[])
{
    pid_t pid = fork();
    if (pid == -1) // Child failed to start.
        return EXIT_FAILURE;

    if (pid != 0)
    {
        // We are in the parent. Child started. Wait for it.
        pid_t ret;
        int status;
        while ((ret = waitpid(pid, &status, 0)) == -1)
        {
            if (errno != EINTR)
                break;
        }
        if (ret != -1)
        {
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
        }
        printf("Child issues!\n");

        return EXIT_FAILURE; // Not sure what status of child is.
    }
    else // We are in the child process. Start the program the parent wants to run.
    {

        if (execv(prog, const_cast<char**>(args)) == -1) // Does not return.
        {
            perror("unreachable 0\n");
            _Exit(127);
        }
        printf("unreachable 2\n");
    }
    printf("unreachable 2\n");
    // Unreachable.
    assert(false);
    exit(EXIT_FAILURE);
    return EXIT_FAILURE;
}
#    endif // !USE_SHELL_API

static
bool
extract_gz_file(const std::string&, const std::string& gz_file, const std::string&)
{
    __builtin_trap() /* STUB: not implemented */;
}

#  endif // !_WIN32

bool
remote_download(const std::string& version, char* error_buffer)
{
    __builtin_trap() /* STUB: not implemented */;
}

bool
remote_install(const std::string& version)
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // HAS_REMOTE_API

static
std::string
get_version(const std::string& path)
{
    __builtin_trap() /* STUB: not implemented */;
}

static
std::unique_ptr<tzdb>
init_tzdb()
{
    __builtin_trap() /* STUB: not implemented */;
}

const tzdb&
reload_tzdb()
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // !USE_OS_TZDB

const tzdb&
get_tzdb()
{
    __builtin_trap() /* STUB: not implemented */;
}

namespace {

class recursion_limiter
{
    unsigned depth_ = 0;
    unsigned limit_;

    class restore_recursion_depth;

public:
    recursion_limiter(recursion_limiter const&) = delete;
    recursion_limiter& operator=(recursion_limiter const&) = delete;

    explicit constexpr recursion_limiter(unsigned limit) noexcept;

    restore_recursion_depth count();
};

class recursion_limiter::restore_recursion_depth
{
    recursion_limiter* rc_;

public:
    ~restore_recursion_depth();
    restore_recursion_depth(restore_recursion_depth&&) = default;

    explicit restore_recursion_depth(recursion_limiter* rc) noexcept;
};

inline
recursion_limiter::restore_recursion_depth::~restore_recursion_depth()
{
    --(rc_->depth_);
}

inline
recursion_limiter::restore_recursion_depth::restore_recursion_depth(recursion_limiter* rc)
                                                                                  noexcept
    : rc_{rc}
{
    __builtin_trap() /* STUB: not implemented */;
}

inline
constexpr
recursion_limiter::recursion_limiter(unsigned limit) noexcept
    : limit_{limit}
{
    
}

inline
recursion_limiter::restore_recursion_depth
recursion_limiter::count()
{
    __builtin_trap() /* STUB: not implemented */;
}

}  // unnamed namespace

const time_zone*
#if HAS_STRING_VIEW
tzdb::locate_zone(std::string_view tz_name) const
#else
tzdb::locate_zone(const std::string& tz_name) const
#endif
{
    __builtin_trap() /* STUB: not implemented */;
}

const time_zone*
#if HAS_STRING_VIEW
locate_zone(std::string_view tz_name)
#else
locate_zone(const std::string& tz_name)
#endif
{
    __builtin_trap() /* STUB: not implemented */;
}

#if USE_OS_TZDB

std::ostream&
operator<<(std::ostream& os, const tzdb& db)
{
    os << "Version: " << db.version << "\n\n";
    for (const auto& x : db.zones)
        os << x << '\n';
    os << '\n';
    for (const auto& x : db.leap_seconds)
        os << x << '\n';
    return os;
}

#else  // !USE_OS_TZDB

std::ostream&
operator<<(std::ostream& os, const tzdb& db)
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // !USE_OS_TZDB

// -----------------------

#ifdef _WIN32

static
std::string
getTimeZoneKeyName()
{
    DYNAMIC_TIME_ZONE_INFORMATION dtzi{};
    auto result = GetDynamicTimeZoneInformation(&dtzi);
    if (result == TIME_ZONE_ID_INVALID)
        throw std::runtime_error("current_zone(): GetDynamicTimeZoneInformation()"
                                 " reported TIME_ZONE_ID_INVALID.");
    auto wlen = wcslen(dtzi.TimeZoneKeyName);
    char buf[128] = {};
    assert(sizeof(buf) >= wlen+1);
    wcstombs(buf, dtzi.TimeZoneKeyName, wlen);
    if (strcmp(buf, "Coordinated Universal Time") == 0)
        return "UTC";
    return buf;
}

const time_zone*
tzdb::current_zone() const
{
    std::string win_tzid = getTimeZoneKeyName();
    std::string standard_tzid;
    if (!native_to_standard_timezone_name(win_tzid, standard_tzid))
    {
        std::string msg;
        msg = "current_zone() failed: A mapping from the Windows Time Zone id \"";
        msg += win_tzid;
        msg += "\" was not found in the time zone mapping database.";
        throw std::runtime_error(msg);
    }
    return locate_zone(standard_tzid);
}

#else  // !_WIN32

#if HAS_STRING_VIEW

static
std::string_view
extract_tz_name(char const* rp)
{
    __builtin_trap() /* STUB: not implemented */;
}

#else  // !HAS_STRING_VIEW

static
std::string
extract_tz_name(char const* rp)
{
    using namespace std;
    string result = rp;
    CONSTDATA char zoneinfo[] = "zoneinfo";
    size_t pos = result.rfind(zoneinfo);
    if (pos == result.npos)
        throw runtime_error(
            "current_zone() failed to find \"zoneinfo\" in " + result);
    pos = result.find('/', pos);
    result.erase(0, pos + 1);
    return result;
}

#endif  // HAS_STRING_VIEW

static
bool
sniff_realpath(const char* timezone)
{
    __builtin_trap() /* STUB: not implemented */;
}

const time_zone*
tzdb::current_zone() const
{
    __builtin_trap() /* STUB: not implemented */;
}

#endif  // !_WIN32

const time_zone*
current_zone()
{
    __builtin_trap() /* STUB: not implemented */;
}

}  // namespace date

#if defined(__GNUC__) && __GNUC__ < 5
# pragma GCC diagnostic pop
#endif
