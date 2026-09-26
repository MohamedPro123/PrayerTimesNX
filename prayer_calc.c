#include "prayer_calc.h"
#include <math.h>
#include <stddef.h> // NULL

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

// ---------------------------------------------------------------------------
// Julian Day Number at 0h UT for a given Gregorian calendar date.
// Standard astronomical algorithm.
// ---------------------------------------------------------------------------
static double julianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int a = year / 100;
    int b = 2 - a + a / 4;
    return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) +
           day + b - 1524.5;
}

// ---------------------------------------------------------------------------
// Approximate solar position (declination + equation of time) for a given
// Julian Day, using the standard low-precision solar coordinates formulas.
// Accurate to roughly +/- 1 minute of time, which is the same order of
// accuracy most prayer-time calculators (online or offline) target.
// ---------------------------------------------------------------------------
static void sunPosition(double jd, double *outDeclinationDeg, double *outEqTimeHours) {
    double n = jd - 2451545.0; // days since J2000.0

    double g = fmod(357.529 + 0.98560028 * n, 360.0);
    double q = fmod(280.459 + 0.98564736 * n, 360.0);
    double L = fmod(q + 1.915 * sin(g * DEG2RAD) + 0.020 * sin(2 * g * DEG2RAD), 360.0);
    double e = 23.439 - 0.00000036 * n;

    double decl = asin(sin(e * DEG2RAD) * sin(L * DEG2RAD)) * RAD2DEG;

    double raDeg = atan2(cos(e * DEG2RAD) * sin(L * DEG2RAD), cos(L * DEG2RAD)) * RAD2DEG;
    if (raDeg < 0) raDeg += 360.0;
    double raHours = raDeg / 15.0;

    double eqT = (q / 15.0) - raHours;
    while (eqT > 12.0) eqT -= 24.0;
    while (eqT < -12.0) eqT += 24.0;

    *outDeclinationDeg = decl;
    *outEqTimeHours = eqT;
}

// Hour angle (in hours) for the sun to reach a given altitude above the
// horizon (positive) or below it (negative), at a given latitude/declination.
// *outValid is set to 0 if the sun never actually reaches that altitude on
// this day at this latitude (before we clamp the ratio into [-1,1]) — this
// is the signal high-latitude adjustment rules key off of.
static double hourAngleForAltitudeEx(double altitudeDeg, double latDeg, double declDeg, int *outValid) {
    double num = sin(altitudeDeg * DEG2RAD) - sin(latDeg * DEG2RAD) * sin(declDeg * DEG2RAD);
    double den = cos(latDeg * DEG2RAD) * cos(declDeg * DEG2RAD);
    double cosH = (den != 0.0) ? (num / den) : 2.0; // treat den==0 as invalid
    int valid = (cosH >= -1.0 && cosH <= 1.0);
    if (outValid) *outValid = valid;
    if (cosH > 1.0) cosH = 1.0;
    if (cosH < -1.0) cosH = -1.0;
    return acos(cosH) * RAD2DEG / 15.0;
}

static double hourAngleForAltitude(double altitudeDeg, double latDeg, double declDeg) {
    return hourAngleForAltitudeEx(altitudeDeg, latDeg, declDeg, NULL);
}

// ---------------------------------------------------------------------------
// Per-method twilight angles. Numbering matches the Aladhan API so existing
// CALC_METHOD values carry over directly.
// ---------------------------------------------------------------------------
static void anglesForMethod(int method, double *fajrAngle, double *ishaAngle, int *ishaIsFixedMinutes, double *ishaFixedMinutes) {
    *ishaIsFixedMinutes = 0;
    *ishaFixedMinutes = 0.0;

    switch (method) {
        case METHOD_KARACHI: *fajrAngle = 18.0; *ishaAngle = 18.0; break;
        case METHOD_MWL:     *fajrAngle = 18.0; *ishaAngle = 17.0; break;
        case METHOD_UMM_AL_QURA:
            *fajrAngle = 18.5; *ishaAngle = 0.0;
            *ishaIsFixedMinutes = 1; *ishaFixedMinutes = 90.0; break;
        case METHOD_EGYPTIAN: *fajrAngle = 19.5; *ishaAngle = 17.5; break;
        case METHOD_TEHRAN:   *fajrAngle = 17.7; *ishaAngle = 14.0; break;
        case METHOD_DIYANET:  *fajrAngle = 18.0; *ishaAngle = 17.0; break; // approx.
        case METHOD_ISNA:
        default: *fajrAngle = 15.0; *ishaAngle = 15.0; break; // ISNA (default/fallback)
    }
}

const char *prayerCalcMethodName(int method) {
    switch (method) {
        case METHOD_KARACHI:     return "Karachi";
        case METHOD_MWL:         return "Muslim World League";
        case METHOD_UMM_AL_QURA: return "Umm Al-Qura (Makkah)";
        case METHOD_EGYPTIAN:    return "Egyptian Authority";
        case METHOD_TEHRAN:      return "Tehran";
        case METHOD_DIYANET:     return "Diyanet (Turkey)";
        case METHOD_ISNA:        return "ISNA (North America)";
        default:                 return "ISNA (North America)";
    }
}

const char *prayerCalcHighLatName(int rule) {
    switch (rule) {
        case HIGHLAT_MIDDLE_OF_NIGHT: return "Middle of the Night";
        case HIGHLAT_ONE_SEVENTH:     return "One-Seventh of Night";
        case HIGHLAT_ANGLE_BASED:     return "Angle-Based";
        case HIGHLAT_NONE:
        default:                     return "None";
    }
}

void prayerCalcCompute(double lat, double lon, double utcOffsetHours,
                        int year, int month, int day,
                        int method, int asrHanafi, int highLatRule,
                        PrayerCalcResult *out) {
    out->ok = 0;

    double jd = julianDay(year, month, day);
    // Refine using an approximate solar-noon estimate, then recompute once
    // more at that better time -- standard two-pass refinement used by most
    // simple implementations to squeeze out most of the equation-of-time error.
    double decl, eqT;
    sunPosition(jd, &decl, &eqT);

    double dhuhr = 12.0 - (lon / 15.0) - eqT + utcOffsetHours;

    // Second pass centered closer to local noon for slightly better accuracy.
    double jdNoon = jd + (dhuhr - utcOffsetHours) / 24.0;
    sunPosition(jdNoon, &decl, &eqT);
    dhuhr = 12.0 - (lon / 15.0) - eqT + utcOffsetHours;

    double fajrAngle, ishaAngle, ishaFixedMinutes;
    int ishaIsFixed;
    anglesForMethod(method, &fajrAngle, &ishaAngle, &ishaIsFixed, &ishaFixedMinutes);

    int fajrValid = 1, ishaValid = 1;
    double hFajr = hourAngleForAltitudeEx(-fajrAngle, lat, decl, &fajrValid);
    double hTwilight = hourAngleForAltitude(-0.833, lat, decl); // sunrise/sunset
    double hIsha = ishaIsFixed ? 0.0 : hourAngleForAltitudeEx(-ishaAngle, lat, decl, &ishaValid);
    if (ishaIsFixed) ishaValid = 1;

    // Approximate night length: 24h minus the (sunrise-to-sunset) day arc.
    double night = 24.0 - 2.0 * hTwilight;
    if (night < 0.0) night = 0.0;

    if (highLatRule != HIGHLAT_NONE && (!fajrValid || !ishaValid)) {
        double fajrPortion, ishaPortion;
        switch (highLatRule) {
            case HIGHLAT_MIDDLE_OF_NIGHT:
                fajrPortion = ishaPortion = 0.5;
                break;
            case HIGHLAT_ONE_SEVENTH:
                fajrPortion = ishaPortion = 1.0 / 7.0;
                break;
            case HIGHLAT_ANGLE_BASED:
            default:
                fajrPortion = fajrAngle / 60.0;
                ishaPortion = ishaAngle / 60.0;
                break;
        }
        if (!fajrValid) hFajr = hTwilight + fajrPortion * night;
        if (!ishaValid && !ishaIsFixed) hIsha = hTwilight + ishaPortion * night;
    }

    double shadowFactor = asrHanafi ? 2.0 : 1.0;
    double diff = fabs(lat - decl) * DEG2RAD;
    double asrAltitude = atan(1.0 / (shadowFactor + tan(diff))) * RAD2DEG;
    double hAsr = hourAngleForAltitude(asrAltitude, lat, decl);

    out->fajr = dhuhr - hFajr;
    out->sunrise = dhuhr - hTwilight;
    out->dhuhr = dhuhr;
    out->asr = dhuhr + hAsr;
    out->maghrib = dhuhr + hTwilight;
    out->isha = ishaIsFixed ? (out->maghrib + ishaFixedMinutes / 60.0) : (dhuhr + hIsha);

    out->ok = 1;
}

// ---------------------------------------------------------------------------
// Qibla direction.
// ---------------------------------------------------------------------------
#define KAABA_LAT 21.4225
#define KAABA_LON 39.8262

double prayerCalcQiblaBearing(double lat, double lon) {
    double phi1 = lat * DEG2RAD;
    double phi2 = KAABA_LAT * DEG2RAD;
    double deltaLambda = (KAABA_LON - lon) * DEG2RAD;

    double y = sin(deltaLambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(deltaLambda);

    double bearing = atan2(y, x) * RAD2DEG;
    bearing = fmod(bearing + 360.0, 360.0); // normalize to [0, 360)
    return bearing;
}
