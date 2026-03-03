/**
 * @file DataPoint.h
 * @brief Core data structures: DataPoint, Leg, and associated enumerations.
 *
 * Every CSV line produced by the Elite Dangerous logger is parsed into a
 * DataPoint.  Groups of consecutive, same-direction Ship points are collected
 * into Leg objects by SurveyData.  The FlightMode and LegDir enumerations tag
 * each record with its context.
 *
 * No computation happens here; this file is pure data layout.
 */
#pragma once
#include <QString>
#include <QDateTime>


// =============================================================================
// FlightMode
// =============================================================================

/**
 * @brief Identifies which vehicle (or body) the player was using when a point
 *        was recorded.
 *
 * The string values come directly from the CSV field produced by the logger:
 *   - "ship"    → FlightMode::Ship
 *   - "srv"     → FlightMode::SRV
 *   - "on_foot" → FlightMode::OnFoot
 *   - anything else → FlightMode::Unknown  (record is rejected by DataPoint::valid())
 */
enum class FlightMode { Ship, SRV, OnFoot, Unknown };

/**
 * @brief Convert a raw CSV mode string to the FlightMode enum.
 *
 * Called by CsvTailReader::parseLine() for every incoming record.
 * Returns FlightMode::Unknown for any unrecognised string, which causes
 * DataPoint::valid() to return false and the record to be discarded.
 *
 * @param s  The mode field as read from the CSV (already trimmed of whitespace).
 * @return   The corresponding FlightMode value.
 */
inline FlightMode modeFromString(const QString& s)
{
    if (s == "ship")     return FlightMode::Ship;
    if (s == "srv")      return FlightMode::SRV;
    if (s == "on_foot")  return FlightMode::OnFoot;
    return FlightMode::Unknown;
}


// =============================================================================
// DataPoint
// =============================================================================

/**
 * @brief One telemetry record from the CSV logger.
 *
 * A DataPoint is created for every non-header line of the CSV file.  Raw fields
 * (lat, lon, altitude, heading) come directly from the CSV.  Two derived fields
 * (distDeg, azimuthDeg) are computed by SurveyData::addPoint() once the base
 * coordinates are known.
 *
 * ### Altitude reliability flag
 * Elite Dangerous reports altitude in two modes:
 *   - **Raycast** (altRaycast = true): the altitude is measured by a raycast
 *     against the terrain mesh.  This is geometrically accurate.
 *   - **Radius** (altRaycast = false): the altitude is derived from orbital
 *     mechanics (ship altitude above the datum sphere), ignoring terrain
 *     relief.  This value can be hundreds of metres off over rugged terrain
 *     and should be excluded from profile plots.  The game sets bit 29 of the
 *     status flags when radius mode is active.
 *
 * ### Derived fields (set by SurveyData)
 * @see SurveyData::distDeg()    — populates DataPoint::distDeg
 * @see SurveyData::azimuthDeg() — populates DataPoint::azimuthDeg
 */
struct DataPoint
{

    /** @brief UTC timestamp of the record (ISO 8601 string in CSV). */
    QDateTime  timestamp;

    /**
     * @brief Body name (optional, on_foot only).
     * Only meaningful for FlightMode::OnFoot records; zero otherwise.
     */
    double     body = 0.0;

    /** @brief Vehicle/mode the player was in when this record was logged. */
    FlightMode mode       = FlightMode::Unknown;

    /** @brief Latitude in decimal degrees (−90 … +90). */
    double     lat        = 0.0;

    /** @brief Longitude in decimal degrees (−180 … +180). */
    double     lon        = 0.0;

    /**
     * @brief Altitude above terrain in metres.
     *
     * For FlightMode::OnFoot this field is always 0; use DataPoint::gravity
     * as the relevant scalar instead.
     */
    double     altitude   = 0.0;

    /**
     * @brief True when the altitude was measured by terrain raycast.
     *
     * When false the value comes from the orbital-radius formula and is
     * unreliable over rough terrain.  Profile plots should skip points with
     * altRaycast == false.
     */
    bool       altRaycast = true;

    /** @brief Ship/SRV/suit compass heading in degrees (0 = North, clockwise). */
    int        heading    = 0;

    /**
     * @brief Surface gravity relative to 1 g (Earth standard).
     *
     * Only meaningful for FlightMode::OnFoot records; zero otherwise.
     */
    double     gravity    = 0.0;

    /** @brief Angular distance from the survey base in degrees of arc.
     *
     * Computed by SurveyData::distDeg() using an equirectangular
     * approximation that corrects for East-West compression at the base
     * latitude.  The formula gives a distance in the same units as latitude
     * degrees, which is useful because it is independent of the planet's
     * radius.
     *
     * Set to 0.0 until SurveyData::addPoint() processes this record.
     */
    double     distDeg    = 0.0;

    /**
     * @brief Surface temperature in degrees Celsius (optional, on_foot only).
     * Only meaningful for FlightMode::OnFoot records; zero otherwise.
     */
    double     temperature = 0.0;

    /**
     * @brief Geometric bearing from the survey base to this point, in degrees
     *        [0, 360).  0° = North, 90° = East (right-hand convention).
     *
     * Computed by SurveyData::azimuthDeg() using atan2 with the same
     * East-West latitude correction as distDeg.  Used by the azimuth-slice
     * plot: points are bucketed by this bearing so that all data collected
     * along a particular direction from the base can be viewed together.
     *
     * Set to 0.0 until SurveyData::addPoint() processes this record.
     */
    double     azimuthDeg = 0.0;

    /**
     * @brief Returns true only if the record is usable.
     *
     * A record is invalid when the mode field was unrecognised ("Unknown")
     * or when the timestamp could not be parsed (null QDateTime).
     * CsvTailReader discards invalid records silently (or emits parseError).
     */
    bool valid() const { return mode != FlightMode::Unknown && !timestamp.isNull(); }
};


// =============================================================================
// LegDir / Leg
// =============================================================================

/**
 * @brief Direction of travel along a survey leg.
 *
 * SurveyData classifies each ship point as Forward or Backward relative to
 * the base position using a linear regression on recent distDeg values
 * (see SurveyData::computeDirection()).
 */
enum class LegDir { Forward, Backward, Unknown };

/**
 * @brief A contiguous run of Ship points with a constant heading bucket and
 *        direction of travel.
 *
 * A new Leg is started whenever:
 *   - The heading bucket changes (ship turns to a different bearing), or
 *   - Three consecutive samples confirm a reversal of travel direction.
 *
 * The heading bucket is the compass heading rounded to the nearest multiple
 * of the bin width (e.g. 5° when using 72 bins).
 *
 * @see SurveyData — for the state machine that creates and closes Legs.
 */
struct Leg
{
    /**
     * @brief Rounded heading that defines this leg, in degrees.
     *
     * Always a multiple of the bin width (360 / numBins).  For example, with
     * 72 bins (5°/bin), a ship flying at 177° is assigned bucket 175°.
     */
    int            headingBucket = 0;

    /** @brief Forward / Backward / Unknown (transitional only). */
    LegDir         dir           = LegDir::Unknown;

    /** @brief Ordered list of Ship DataPoints that make up this leg. */
    QVector<DataPoint> points;

    /** @brief Convenience accessor — true when the ship was moving away from base. */
    bool isForward()  const { return dir == LegDir::Forward;  }

    /** @brief Convenience accessor — true when the ship was moving toward base. */
    bool isBackward() const { return dir == LegDir::Backward; }
};
