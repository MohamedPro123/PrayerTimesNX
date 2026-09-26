#ifndef PRAYER_CALC_H
#define PRAYER_CALC_H

// Offline prayer-time calculation using standard solar-position astronomy
// (the same class of sun-angle formulas essentially every prayer-time
// calculator, online or offline, is built on). No network required —
// only latitude/longitude, a UTC offset for the target location, and a
// calendar date.

typedef struct {
    int ok;
    // Local clock time in fractional hours, e.g. 5.25 = 05:15.
    double fajr;
    double sunrise;
    double dhuhr;
    double asr;
    double maghrib;
    double isha;
} PrayerCalcResult;

// method uses the same numbering as the Aladhan API, so it's a drop-in
// replacement for the CALC_METHOD you were already using online:
#define METHOD_KARACHI      1  // University of Islamic Sciences, Karachi
#define METHOD_ISNA         2  // Islamic Society of North America    [default]
#define METHOD_MWL          3  // Muslim World League
#define METHOD_UMM_AL_QURA  4  // Umm Al-Qura University, Makkah (fixed Isha interval)
#define METHOD_EGYPTIAN     5  // Egyptian General Authority of Survey
#define METHOD_TEHRAN       7  // Institute of Geophysics, University of Tehran
#define METHOD_DIYANET      13 // Diyanet Isleri Baskanligi, Turkey
// Unrecognized codes fall back to ISNA.

// asrHanafi: 0 = standard/Shafi (shadow factor 1), 1 = Hanafi (shadow factor 2).

// High-latitude adjustment rule. Near/above the polar circles the sun may
// never dip far enough below the horizon to reach the Fajr/Isha twilight
// angle for a given method, which would otherwise leave Fajr and Isha
// undefined (or absurdly close to Dhuhr). These rules approximate them the
// same way most prayer-time calculators do, only kicking in when the plain
// angle-based calculation is actually invalid for the day/location:
#define HIGHLAT_NONE             0 // no adjustment (may clamp to horizon)
#define HIGHLAT_MIDDLE_OF_NIGHT  1 // Fajr/Isha = midpoint of the night
#define HIGHLAT_ONE_SEVENTH      2 // Fajr/Isha = 1/7th of the night from dawn/dusk
#define HIGHLAT_ANGLE_BASED      3 // Fajr/Isha = (angle/60) portion of the night

// lat/lon in decimal degrees. utcOffsetHours is the target location's
// offset from UTC *including DST* for the given date (get this from the
// console's own timezone database for known cities, or a fixed manual
// offset for custom/typed-in coordinates — see getLocalDateAndOffset() /
// getLocalDateAndOffsetFixed() in main.c).
void prayerCalcCompute(double lat, double lon, double utcOffsetHours,
                        int year, int month, int day,
                        int method, int asrHanafi, int highLatRule,
                        PrayerCalcResult *out);

// Short display names, for building settings menus. Never returns NULL.
const char *prayerCalcMethodName(int method);
const char *prayerCalcHighLatName(int rule);

// Initial great-circle bearing (degrees, 0-360, clockwise from true North)
// from a given lat/lon to the Kaaba in Makkah -- i.e. which way to face for
// Qibla. Standard spherical bearing formula; same result any Qibla app uses.
double prayerCalcQiblaBearing(double lat, double lon);

#endif
