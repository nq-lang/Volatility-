#pragma once
// session.h - US equity regular-trading-hours session context derived from a timestamp (shared Time logic, workfile §7.2/§7.3).
// RTH 09:30-16:00 America/New_York. DST per current US rule (2nd Sun Mar 02:00 -> 1st Sun Nov 02:00). Pure: no clock reads.
// Futures trade nearly 24h; the options-derived RTH structure used here is by design RTH-anchored.
#include <chrono>
#include <cmath>
#include <string>
#include "vt/types.h"
namespace vt::sess {
enum class Phase { Closed, Open, Midday, IntoClose };          // Open: first 60 min; IntoClose: final 90 min
inline const char* phase_name(Phase p) { switch (p) { case Phase::Open: return "open"; case Phase::Midday: return "midday"; case Phase::IntoClose: return "into the close"; default: return "outside regular hours"; } }
struct Session {
    long long utc = 0; int year = 0, month = 0, day = 0, weekday = 0;     // weekday 0=Mon .. 6=Sun (ET calendar date)
    int et_min = 0;                                                      // minutes since ET midnight
    bool rth = false; int since_open = 0, to_close = 0;                  // minutes (0 outside RTH)
    Phase phase = Phase::Closed; std::string as_of;                      // "Tue 20 Oct 10:10 ET"
};
static constexpr int OPEN_MIN = 9 * 60 + 30, CLOSE_MIN = 16 * 60, RTH_MIN = CLOSE_MIN - OPEN_MIN;
inline long long days_from_civil(int y, unsigned m, unsigned d) { y -= m <= 2; const long long era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = (unsigned)(y - era * 400); const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (long long)doe - 719468; }
inline void civil_from_days(long long z, int& y, int& m, int& d) { z += 719468; const long long era = (z >= 0 ? z : z - 146096) / 146097; const unsigned doe = (unsigned)(z - era * 146097); const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; y = (int)(yoe + era * 400); const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); const unsigned mp = (5 * doy + 2) / 153; d = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); y += m <= 2; }
inline int weekday_of_days(long long days) { return (int)(((days % 7) + 7 + 3) % 7); }                // 1970-01-01 was a Thursday -> 3 (Mon=0)
inline bool us_dst(long long utc) {
    int y, m, d; civil_from_days(utc / 86400, y, m, d);
    auto nth_sunday = [&](int mon, int n) { long long first = days_from_civil(y, mon, 1); int wd = weekday_of_days(first); int off = (6 - wd + 7) % 7; return first + off + 7 * (n - 1); };
    long long start = nth_sunday(3, 2) * 86400 + 7 * 3600, end = nth_sunday(11, 1) * 86400 + 6 * 3600;      // 02:00 EST = 07:00Z ; 02:00 EDT = 06:00Z
    return utc >= start && utc < end;
}
inline const char* dow_name(int w) { static const char* n[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}; return n[((w % 7) + 7) % 7]; }
inline const char* mon_name(int m) { static const char* n[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; return n[(m - 1 + 12) % 12]; }
inline std::string hhmm(int min) { min = ((min % 1440) + 1440) % 1440; char b[16]; std::snprintf(b, sizeof b, "%02d:%02d", min / 60, min % 60); return b; }
inline Session from_utc(long long utc) {
    Session s; s.utc = utc; long long loc = utc - (us_dst(utc) ? 4 : 5) * 3600; long long days = loc >= 0 ? loc / 86400 : -((-loc + 86399) / 86400);
    int sec = (int)(loc - days * 86400); s.et_min = sec / 60; civil_from_days(days, s.year, s.month, s.day); s.weekday = weekday_of_days(days);
    s.rth = s.weekday < 5 && s.et_min >= OPEN_MIN && s.et_min < CLOSE_MIN;
    if (s.rth) { s.since_open = s.et_min - OPEN_MIN; s.to_close = CLOSE_MIN - s.et_min; s.phase = s.since_open < 60 ? Phase::Open : s.to_close <= 90 ? Phase::IntoClose : Phase::Midday; }
    s.as_of = std::string(dow_name(s.weekday)) + " " + std::to_string(s.day) + " " + mon_name(s.month) + " " + hhmm(s.et_min) + " ET"; return s;
}
inline Session from_ts(TimePoint t) { return from_utc((long long)std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count()); }
// label for an expiry `dte` calendar days after the session date, 16:00 ET close
inline std::string expiry_label(const Session& s, int dte) {
    long long days = days_from_civil(s.year, (unsigned)s.month, (unsigned)s.day) + dte; int y, m, d; civil_from_days(days, y, m, d);
    return std::string(dow_name(weekday_of_days(days))) + " " + std::to_string(d) + " " + mon_name(m);
}
}
