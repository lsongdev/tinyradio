#pragma once
#include <stdint.h>
// Proleptic Gregorian dates <-> days since 1970-01-01, after H. Hinnant's
// days_from_civil / civil_from_days. Valid for any representable year.
static inline int64_t calendar_days(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = (unsigned)(year - era * 400);
  const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + (int64_t)day_of_era - 719468;
}
static inline void calendar_date(int64_t days, int64_t* year, unsigned* month, unsigned* day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = (unsigned)(days - era * 146097);
  const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned shifted_month = (5 * day_of_year + 2) / 153;
  *day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  *month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  *year = (int64_t)year_of_era + era * 400 + (*month <= 2);
}
// Floor division, so times before 1970 still land on the right day.
static inline int64_t calendar_day_of(int64_t seconds) {
  return seconds >= 0 ? seconds / 86400 : -((-seconds + 86399) / 86400);
}
