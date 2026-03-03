/**
 * @file SurveyData.cpp
 * @brief Implementation of SurveyData — geometry helpers, data ingestion,
 *        and the heading/direction-change leg-detection state machine.
 */
#include "SurveyData.h"
#include <algorithm>
#include <cmath>

/// Degrees-to-radians conversion factor.
static constexpr double DEG2RAD = M_PI / 180.0;


// =============================================================================
// Geometry helpers (static)
// =============================================================================

/**
 * @brief Round a compass heading to the nearest heading-bin centre.
 *
 * ### Step-by-step
 * 1. Normalise to [0, 360):
 *    @code
 *      heading = ((heading % 360) + 360) % 360
 *    @endcode
 *    The double modulo handles both negative values (e.g. −5° → 355°) and
 *    values ≥ 360 (e.g. 365° → 5°).
 *
 * 2. Compute the bin width:
 *    @code
 *      binWidth = 360.0 / m_numBins
 *    @endcode
 *
 * 3. Round to the nearest multiple of binWidth:
 *    @code
 *      bucket = qRound(heading / binWidth) * binWidth   (mod 360)
 *    @endcode
 *    `qRound` rounds 0.5 up, consistent with conventional rounding.
 *    The final `% 360` collapses 360° back to 0° (edge case: h = 357.5° with
 *    a 5° bin would otherwise round to 360°).
 *
 * Example (m_numBins = 72, binWidth = 5°):
 *   | heading | heading/5 | round | *5  | result |
 *   |---------|-----------|-------|-----|--------|
 *   | 177°    | 35.4      | 35    | 175 | 175°   |
 *   | 178°    | 35.6      | 36    | 180 | 180°   |
 *   | 357°    | 71.4      | 71    | 355 | 355°   |
 *   | 358°    | 71.6      | 72    | 360 |   0°   |  ← mod 360
 */
int SurveyData::headingBucket(int heading) const
{
    heading = ((heading % 360) + 360) % 360;
    double binWidth = 360.0 / m_numBins;
    return static_cast<int>(qRound(heading / binWidth) * binWidth) % 360;
}

/**
 * @brief Equirectangular angular distance from (baseLat, baseLon) to (lat, lon).
 *
 * ### Why equirectangular?
 * A planet's radius is not available in the CSV data.  Using metres would
 * require hardcoding a radius guess.  Angular degrees of arc are radius-
 * independent: any two planets give the same distDeg for geometrically
 * identical survey patterns.
 *
 * ### Derivation
 * In a small region around (baseLat, baseLon), we can treat the surface as flat:
 *   - A 1° latitude step corresponds to the same arc length everywhere.
 *   - A 1° longitude step corresponds to cos(baseLat) × (1° latitude step)
 *     because meridians converge toward the poles.
 *
 * Therefore an isometric (equal-scale) coordinate frame at the base is:
 * @code
 *   dlat = lat  - baseLat                         [°, North-South]
 *   dlon = (lon - baseLon) * cos(baseLat * π/180) [°, East-West, corrected]
 * @endcode
 * The angular distance is the Euclidean norm in this frame:
 * @code
 *   dist = sqrt(dlat² + dlon²)  [°]
 * @endcode
 *
 * @param lat      Point latitude  [°].
 * @param lon      Point longitude [°].
 * @param baseLat  Base latitude   [°].
 * @param baseLon  Base longitude  [°].
 * @return         Angular distance in degrees of arc (always ≥ 0).
 */
double SurveyData::distDeg(double lat, double lon, double baseLat, double baseLon)
{
    double cosLat = std::cos(baseLat * DEG2RAD);
    double dlat   = lat  - baseLat;            // North-South component [°]
    double dlon   = (lon - baseLon) * cosLat;  // East-West component, corrected [°]
    return std::sqrt(dlat * dlat + dlon * dlon);
}

/**
 * @brief Geometric bearing (azimuth) from the base to a point, [0°, 360°).
 *
 * ### Derivation
 * Using the same isometric frame as distDeg():
 * @code
 *   dlat = lat  - baseLat
 *   dlon = (lon - baseLon) * cos(baseLat * π/180)
 * @endcode
 *
 * The bearing is the angle measured clockwise from North:
 * @code
 *   az = atan2(dlon, dlat) * (180/π)
 * @endcode
 * Note the argument order: atan2(**dlon**, **dlat**) — not the conventional
 * maths order atan2(y, x) — because:
 *   - In standard maths, 0° is East and angles are counter-clockwise.
 *   - In compass convention, 0° is North and angles are clockwise.
 * Swapping the arguments rotates the frame by 90° and mirrors it, converting
 * from maths convention to compass convention in one step.
 *
 * The result of atan2 is in [−π, π] radians, i.e. [−180°, 180°].  Adding
 * 360° when the result is negative maps it to [0°, 360°).
 *
 * @param lat      Point latitude  [°].
 * @param lon      Point longitude [°].
 * @param baseLat  Base latitude   [°].
 * @param baseLon  Base longitude  [°].
 * @return         Bearing in degrees [0°, 360°). 0° = North, 90° = East.
 */
double SurveyData::azimuthDeg(double lat, double lon, double baseLat, double baseLon)
{
    double cosLat = std::cos(baseLat * DEG2RAD);
    double dlat   = lat  - baseLat;
    double dlon   = (lon - baseLon) * cosLat;
    double az     = std::atan2(dlon, dlat) * (180.0 / M_PI);  // [-180, 180]
    return az < 0.0 ? az + 360.0 : az;                         // normalise → [0, 360)
}


// =============================================================================
// Constructor / configuration
// =============================================================================

SurveyData::SurveyData(QObject* parent)
    : QObject(parent)
{}

/**
 * @brief Update the base coordinates.  All future distDeg / azimuthDeg values
 *        will be computed relative to this position.
 *
 * Points already in the store are NOT recomputed; this function should
 * therefore be called once before any addPoint() calls.
 */
void SurveyData::setBase(double lat, double lon)
{
    m_baseLat = lat;
    m_baseLon = lon;
}

/**
 * @brief Validate and store the bin count.  Out-of-range values revert to 72.
 */
void SurveyData::setNumBins(int n)
{
    m_numBins = (n >= 1 && n <= 360) ? n : 72;
}


// =============================================================================
// Main entry point
// =============================================================================

/**
 * @brief Ingest one DataPoint.
 *
 * ### Processing pipeline
 *
 * **Step 1 — Compute derived geometry.**
 * The raw point (from the CSV) does not yet have distDeg or azimuthDeg.
 * We make a local copy, fill both fields using the current base, and work
 * with that copy for all subsequent operations.
 *
 * **Step 2 — Mode-agnostic stores.**
 * The point is appended to m_all and to the per-mode vector (m_ship / m_srv
 * / m_foot).  The pointAdded() signal lets the scatter map update immediately.
 *
 * **Step 3 — Azimuth-bucket indexing.**
 * The rounded azimuth bearing is used as an index so that the azimuth-slice
 * plot (AzimuthWidget) can quickly retrieve all points collected along a
 * given direction from the base.  The same headingBucket() rounding function
 * is reused so that both heading and azimuth buckets share the same width.
 *
 * **Step 4 — SRV / OnFoot: heading-bucket indexing, then return.**
 * These modes are also indexed by the ship heading bucket (the heading field
 * of the SRV/suit sensor) so that the profile plot can overlay them.  Then
 * the function returns — leg detection only makes sense for Ship mode.
 *
 * **Step 5 — Ship: leg detection state machine.**
 * See the detailed comments in the sub-blocks below.
 */
void SurveyData::addPoint(const DataPoint& inPt)
{
    // ── Step 1: derived geometry ───────────────────────────────────────────
    DataPoint pt = inPt;
    pt.distDeg    = distDeg(pt.lat, pt.lon, m_baseLat, m_baseLon);
    pt.azimuthDeg = azimuthDeg(pt.lat, pt.lon, m_baseLat, m_baseLon);

    // ── Step 2: universal stores ───────────────────────────────────────────
    m_all.append(pt);

    switch (pt.mode) {
    case FlightMode::Ship:    m_ship.append(pt);  break;
    case FlightMode::SRV:     m_srv.append(pt);   break;
    case FlightMode::OnFoot:  m_foot.append(pt);  break;
    default: break;
    }

    emit pointAdded(pt);

    // ── Step 3: azimuth-bucket indexing ───────────────────────────────────
    // The azimuthDeg is a continuous [0, 360) float; we bucket it using the
    // same rounding as headings (nearest multiple of bin width).
    int azBucket = headingBucket(static_cast<int>(pt.azimuthDeg+0.5));
    switch (pt.mode) {
    case FlightMode::Ship:    m_shipByAzimuth[azBucket].append(pt);  break;
    case FlightMode::SRV:     m_srvByAzimuth[azBucket].append(pt);   break;
    case FlightMode::OnFoot:  m_footByAzimuth[azBucket].append(pt);  break;
    default: break;
    }

    // ── Step 4: SRV / OnFoot heading-bucket index, then exit ──────────────
    // We index by the heading the suit/SRV reports (pt.heading), not by the
    // geometric azimuth, so that these points appear alongside ship legs flown
    // at the same compass bearing.
    if (pt.mode == FlightMode::SRV || pt.mode == FlightMode::OnFoot) {
        int bucket = headingBucket(pt.heading);
        if (pt.mode == FlightMode::SRV)    m_srvByHeading[bucket].append(pt);
        if (pt.mode == FlightMode::OnFoot) m_footByHeading[bucket].append(pt);
        return;  // no leg detection for non-ship modes
    }

    // ── Step 5: Ship-only leg detection ───────────────────────────────────
    if (pt.mode != FlightMode::Ship) return;

    int bucket = headingBucket(pt.heading);

    // Announce heading the first time it appears.
    if (!m_seenHeadings.contains(bucket)) {
        m_seenHeadings.insert(bucket);
        emit newHeadingDetected(bucket);
    }

    // Maintain the sliding distDeg window.  The oldest entry is dropped once
    // the buffer reaches DIR_WINDOW capacity.
    m_recentDists.append(pt.distDeg);
    if (m_recentDists.size() > DIR_WINDOW)
        m_recentDists.removeFirst();

    if (!m_legStarted) {
        // We don't yet have enough samples to determine direction — wait until
        // the buffer is full, then start the first leg.
        if (m_recentDists.size() >= DIR_WINDOW) {
            LegDir dir = computeDirection();
            startNewLeg(bucket, dir, pt);
        }
        return;
    }

    // ── Heading bucket change → immediate finalisation ─────────────────────
    // If the ship turns and the heading bucket changes, the current leg ends
    // instantly.  We do not wait for direction confirmation here.
    if (bucket != m_activeLeg.headingBucket) {
        finalizeActiveLeg();
        LegDir dir = computeDirection();
        startNewLeg(bucket, dir, pt);
        return;
    }

    // ── Direction change → confirmation required ───────────────────────────
    // We only split a leg when the direction truly reversed, not on momentary
    // GPS noise.  The reversal must be sustained for DIR_CONFIRM consecutive
    // samples before we act.
    LegDir currentDir = computeDirection();
    if (currentDir != m_activeLeg.dir && currentDir != LegDir::Unknown) {
        m_dirChangeCount++;
        if (m_dirChangeCount >= DIR_CONFIRM) {
            // Confirmed reversal — close the current leg, open a new one.
            finalizeActiveLeg();
            startNewLeg(bucket, currentDir, pt);
            m_dirChangeCount = 0;
            return;
        }
    } else {
        // This sample agrees with the current direction; reset the counter.
        m_dirChangeCount = 0;
    }

    // ── Normal case: append to active leg ─────────────────────────────────
    m_activeLeg.points.append(pt);
    emit activeLegUpdated();
}


// =============================================================================
// Direction detection — least-squares linear regression
// =============================================================================

/**
 * @brief Estimate travel direction from the recent distDeg ring buffer.
 *
 * ### Why linear regression?
 * The ship's distDeg changes smoothly over time.  A simple comparison of the
 * first and last sample in the window would be noisy.  Fitting a straight line
 * to all DIR_WINDOW samples and reading its slope gives a statistically more
 * robust estimate.
 *
 * ### Least-squares slope formula
 * Given n samples (x_i, y_i) where x_i = i (sample index) and
 * y_i = m_recentDists[i]:
 *
 * @code
 *   slope = (n·ΣxᵢYᵢ − Σxᵢ·ΣYᵢ) / (n·Σxᵢ² − (Σxᵢ)²)
 * @endcode
 *
 * This is the standard ordinary least-squares (OLS) estimator for the slope
 * of y = a + b·x.  We only need the slope (b), not the intercept (a).
 *
 * ### Threshold
 * A slope of 0.001°/sample corresponds to ~0.1 m/sample on a 3000 km body
 * (1° arc ≈ 52 km → 0.001° ≈ 52 m).  Anything below that magnitude is
 * treated as "hovering" (Unknown direction).
 *
 * ### Degeneracy guard
 * If all x_i values are identical (impossible with the current ring buffer but
 * guarded anyway), the denominator would be zero.  We return Unknown in that
 * case.
 *
 * @return LegDir::Forward if slope > +0.001,
 *         LegDir::Backward if slope < −0.001,
 *         LegDir::Unknown otherwise.
 */
LegDir SurveyData::computeDirection() const
{
    if (m_recentDists.size() < 2) return LegDir::Unknown;

    int n = m_recentDists.size();
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    for (int i = 0; i < n; ++i) {
        sumX  += i;
        sumY  += m_recentDists[i];
        sumXY += i * m_recentDists[i];
        sumX2 += i * i;
    }
    double denom = n * sumX2 - sumX * sumX;
    if (std::abs(denom) < 1e-12) return LegDir::Unknown;  // degenerate case
    double slope = (n * sumXY - sumX * sumY) / denom;

    if (slope >  0.001) return LegDir::Forward;   // distDeg increasing → moving away
    if (slope < -0.001) return LegDir::Backward;  // distDeg decreasing → moving toward
    return LegDir::Unknown;                        // effectively stationary
}


// =============================================================================
// Leg management
// =============================================================================

/**
 * @brief Start a new active leg.
 *
 * Resets all state, sets the heading bucket and direction, and inserts the
 * first point.  Emits activeLegUpdated() so the profile plot can show the
 * single-point live leg immediately.
 */
void SurveyData::startNewLeg(int bucket, LegDir dir, const DataPoint& pt)
{
    m_activeLeg               = Leg{};
    m_activeLeg.headingBucket = bucket;
    m_activeLeg.dir           = dir;
    m_activeLeg.points.append(pt);
    m_legStarted              = true;
    m_dirChangeCount          = 0;
    emit activeLegUpdated();
}

/**
 * @brief Close the active leg and move it to the permanent store.
 *
 * Does nothing if no leg is active or the active leg has no points.
 * Emits legFinalized() so the UI can refresh the profile for the affected
 * heading bucket.
 */
void SurveyData::finalizeActiveLeg()
{
    if (!m_legStarted || m_activeLeg.points.isEmpty()) return;

    int bucket = m_activeLeg.headingBucket;
    m_legs[bucket].append(m_activeLeg);
    m_legStarted = false;
    emit legFinalized(bucket);
}


// =============================================================================
// Accessors
// =============================================================================

/**
 * @brief Return all legs for @p bucket, including the active leg if it matches.
 *
 * Including the active (in-progress) leg gives the profile plot a complete
 * picture even before the current run ends.
 */
QVector<Leg> SurveyData::legsForHeading(int bucket) const
{
    auto it = m_legs.find(bucket);
    QVector<Leg> result = (it != m_legs.end()) ? it.value() : QVector<Leg>{};

    // Append the live leg if it belongs to the same bucket
    if (m_legStarted && m_activeLeg.headingBucket == bucket)
        result.append(m_activeLeg);

    return result;
}

/**
 * @brief Return SRV or OnFoot points for the given heading bucket.
 *
 * Ship points are never returned by this function; use legsForHeading()
 * for ship data.  Returns an empty vector for any other mode.
 */
QVector<DataPoint> SurveyData::pointsForHeading(int bucket, FlightMode mode) const
{
    if (mode == FlightMode::SRV)    return m_srvByHeading.value(bucket);
    if (mode == FlightMode::OnFoot) return m_footByHeading.value(bucket);
    return {};
}

/**
 * @brief Return points for a given azimuth bucket and flight mode.
 *
 * Returns an empty vector for FlightMode::Unknown.
 */
QVector<DataPoint> SurveyData::pointsByAzimuth(int bucket, FlightMode mode) const
{
    if (mode == FlightMode::Ship)   return m_shipByAzimuth.value(bucket);
    if (mode == FlightMode::SRV)    return m_srvByAzimuth.value(bucket);
    if (mode == FlightMode::OnFoot) return m_footByAzimuth.value(bucket);
    return {};
}

/**
 * @brief Return all known heading buckets in ascending order.
 */
QVector<int> SurveyData::knownHeadings() const
{
    QVector<int> out = m_seenHeadings.values().toVector();
    std::sort(out.begin(), out.end());
    return out;
}
