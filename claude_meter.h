#pragma once
// Helpers for claude-usage-meter.yaml (time parsing, formatting, colors, alert sounds).
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include "lvgl.h"

namespace claude_meter {

// Catppuccin Mocha colors
static const uint32_t COL_TEXT = 0xCDD6F4;
static const uint32_t COL_MUTED = 0x6C7086;
static const uint32_t COL_PEACH = 0xFAB387;
static const uint32_t COL_RED = 0xF38BA8;

// Days since 1970-01-01 (Howard Hinnant's algorithm)
inline int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// "2026-10-15T20:10:00+00:00" (optionally with fractional seconds) -> UTC epoch; 0 if it cannot be parsed.
inline int64_t parse_iso(const std::string &s) {
  int Y, M, D, h, mi, sec;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec) != 6)
    return 0;
  int64_t t = days_from_civil(Y, M, D) * 86400 + h * 3600 + mi * 60 + sec;
  size_t p = 19;
  while (p < s.size() && (s[p] == '.' || isdigit(static_cast<unsigned char>(s[p]))))
    p++;
  if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
    int oh = 0, om = 0;
    sscanf(s.c_str() + p + 1, "%d:%d", &oh, &om);
    int off = oh * 3600 + om * 60;
    t += (s[p] == '+') ? -off : off;
  }
  return t;
}

// Time until reset: "2h 05m", "1d 14h", "teraz" (now)
inline std::string countdown(int64_t target, int64_t now) {
  if (target <= 0 || now < 1000000000)
    return "--";
  int64_t left = target - now;
  if (left <= 0)
    return "teraz";
  int d = left / 86400, h = (left % 86400) / 3600, m = (left % 3600) / 60;
  char buf[24];
  if (d > 0)
    snprintf(buf, sizeof(buf), "%dd %dh", d, h);
  else
    snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
  return buf;
}

// Polish short date, e.g. "czw 15 paź"
inline std::string date_pl(int64_t now) {
  if (now < 1000000000)
    return "";
  static const char *days[] = {"nd", "pon", "wt", "śr", "czw", "pt", "sob"};
  static const char *months[] = {"sty", "lut", "mar", "kwi", "maj", "cze",
                                 "lip", "sie", "wrz", "paź", "lis", "gru"};
  time_t tt = static_cast<time_t>(now);
  struct tm tm;
  localtime_r(&tt, &tm);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %d %s", days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon]);
  return buf;
}

inline uint32_t usage_color(float p) {
  if (p >= 85)
    return COL_RED;
  if (p >= 60)
    return COL_PEACH;
  return COL_TEXT;
}

// Sets the percentage label + arc; the arc uses its base color, peach/red above the thresholds.
inline void show_usage(lv_obj_t *label, lv_obj_t *arc, float p, uint32_t base_color) {
  if (std::isnan(p)) {
    lv_label_set_text(label, "--");
    lv_obj_set_style_text_color(label, lv_color_hex(COL_MUTED), LV_PART_MAIN);
    lv_arc_set_value(arc, 0);
    return;
  }
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f%%", p);
  lv_label_set_text(label, buf);
  uint32_t c = usage_color(p);
  lv_obj_set_style_text_color(label, lv_color_hex(c), LV_PART_MAIN);
  int v = static_cast<int>(lroundf(p));
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  lv_arc_set_value(arc, v);
  lv_obj_set_style_arc_color(arc, lv_color_hex(c == COL_TEXT ? base_color : c), LV_PART_INDICATOR);
}

// --- SOUNDS ---
// Alert thresholds (%); each plays once, and again only after usage drops below ALERT_RENEW (limit reset)
static const float ALERT_WARN = 80;
static const float ALERT_CRIT = 95;
static const float ALERT_RENEW = 10;
// Quiet hours: [QUIET_FROM, QUIET_TO) in local time
static const int QUIET_FROM = 23;
static const int QUIET_TO = 7;

enum AlertEvent { EV_NONE = 0, EV_WARN = 1, EV_CRIT = 2, EV_RENEW = 3 };

// RTTTL melodies
static const char *const MELODY_WARN = "warn:d=8,o=6,b=180:e";
static const char *const MELODY_CRIT = "crit:d=16,o=6,b=180:a,p,a,p,a";
static const char *const MELODY_RENEW = "renew:d=8,o=5,b=160:c,e,g,4c6";

inline const char *alert_melody(int ev) {
  switch (ev) {
    case EV_WARN: return MELODY_WARN;
    case EV_CRIT: return MELODY_CRIT;
    case EV_RENEW: return MELODY_RENEW;
    default: return "";
  }
}

inline bool quiet_hour(int hour) { return hour >= QUIET_FROM || hour < QUIET_TO; }

// Updates the alert level (0/1/2) for a new value and returns the event to play.
// A jump from e.g. 50% to 97% plays only the 95% alert. Dropping below ALERT_RENEW after an alert
// resets the level; the renewal melody plays only when renew_melody is set (session limit).
inline int alert_step(float p, int &level, bool renew_melody) {
  if (std::isnan(p))
    return EV_NONE;
  if (p >= ALERT_CRIT) {
    if (level >= 2) return EV_NONE;
    level = 2;
    return EV_CRIT;
  }
  if (p >= ALERT_WARN) {
    if (level >= 1) return EV_NONE;
    level = 1;
    return EV_WARN;
  }
  if (p < ALERT_RENEW && level > 0) {
    level = 0;
    return renew_melody ? EV_RENEW : EV_NONE;
  }
  return EV_NONE;
}

// MDI icon for a weather.* entity state
inline const char *weather_icon(const std::string &s) {
  if (s == "sunny") return "\U000F0599";
  if (s == "clear-night") return "\U000F0594";
  if (s == "partlycloudy") return "\U000F0595";
  if (s == "cloudy") return "\U000F0590";
  if (s == "fog") return "\U000F0591";
  if (s == "hail") return "\U000F0592";
  if (s == "lightning") return "\U000F0593";
  if (s == "lightning-rainy") return "\U000F067E";
  if (s == "pouring") return "\U000F0596";
  if (s == "rainy") return "\U000F0597";
  if (s == "snowy") return "\U000F0598";
  if (s == "snowy-rainy") return "\U000F067F";
  if (s == "windy" || s == "windy-variant") return "\U000F059D";
  return "\U000F05D6";  // alert-circle-outline
}

}  // namespace claude_meter
