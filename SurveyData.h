/**
 * @file SurveyData.h
 * @brief Central data store and leg-detection engine for the survey viewer.
 *
 * SurveyData is the heart of the business logic.  It receives DataPoints one
 * at a time (via addPoint()), computes derived geometry, and runs a state
 * machine that segments the ship's track into directional "legs" — contiguous
 * runs with a stable compass heading and a stable direction of travel (toward
 * or away from the base position).
 *
 * ### Architecture: why a separate data class?
 * All UI widgets are read-only consumers.  Separating the data model from the
 * views means:
 *   - Widgets can be rebuilt / switched without re-processing any data.
 *   - Unit-testing the detection logic requires no Qt GUI at all.
 *   - Multiple views (scatter map, profile plot, azimuth plot) share the same
 *     computed structures without duplication.
 *
 * ### Data flow
 * @code
 * CsvTailReader ──newPoint──► MainWindow::onRawPoint
 *                               └── applyBase (first point only)
 *                               └── SurveyData::addPoint
 *                                     ├── distDeg / azimuthDeg computed
 *                                     ├── all-mode stores updated
 *                                     ├── azimuth-bucket stores updated
 *                                     ├── [Ship only] heading-bucket leg
 *                                     │    detection state machine runs
 *                                     └── signals emitted to UI widgets
 * @endcode
 */
#pragma once
#include "DataPoint.h"
#include <QObject>
#include <QMap>
#include <QSet>
#include <QVector>
#include <cmath>


/**
 * @brief Central data store and survey-leg detection engine.
 *
 * ### Heading buckets
 * To group together points measured on the same "run", compass headings are
 * rounded to the nearest multiple of the bin width (360 / numBins degrees).
 * With the default 72 bins the width is 5°, so headings 178°–182° all map to
 * bucket 180°.
 *
 * ### Leg detection state machine
 * Only Ship-mode points participate in leg detection.  The machine operates
 * on a sliding window (ring buffer) of the last DIR_WINDOW (4) distDeg values
 * and uses a least-squares linear regression slope to decide direction:
 *   - slope > +0.001°/sample → Forward  (moving away from base)
 *   - slope < −0.001°/sample → Backward (moving toward base)
 *   - |slope| ≤ 0.001         → Unknown  (hovering / stationary)
 *
 * A direction change is not immediately accepted: DIR_CONFIRM (3) consecutive
 * samples must agree before the active leg is finalised and a new one started.
 * This avoids false splits caused by short GPS jitter or brief heading wobbles.
 *
 * A heading bucket change, by contrast, finalises the leg immediately.
 *
 * ### Azimuth buckets
 * In addition to heading-based grouping, every point (all modes) is also
 * stored in a bucket indexed by its *geometric* bearing from the base
 * (pointsByAzimuth()).  The same headingBucket() rounding function is reused
 * for this, so both axes share the same bin width.
 */
class SurveyData : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct with an optional Qt parent for memory management.
     * @param parent  Owning QObject; typically the MainWindow.
     */
    explicit SurveyData(QObject* parent = nullptr);

    // =========================================================================
    // Configuration — call before first addPoint()
    // =========================================================================

    /**
     * @brief Set the survey base (reference) position.
     *
     * All distDeg and azimuthDeg values are measured relative to this point.
     * The default coordinates (27.8797°, −35.5017°) were the first test site;
     * they are overwritten by MainWindow::applyBase() before any points arrive.
     *
     * @param lat  Latitude of the base in decimal degrees.
     * @param lon  Longitude of the base in decimal degrees.
     */
    void setBase(double lat, double lon);

    /**
     * @brief Set the number of heading bins (and azimuth bins).
     *
     * The bin width becomes 360 / n degrees.  Typical values:
     *   - n = 36  → 10° bins (coarse; useful for large planets)
     *   - n = 72  → 5° bins (default; good balance of detail vs. noise)
     *   - n = 360 → 1° bins (fine; many buckets, few points each)
     *
     * Must be called before the first addPoint(); changing it mid-stream
     * would break the heading list shown in the UI.
     *
     * @param n  Number of bins in [1, 360].  Values outside that range
     *           silently revert to 72.
     */
    void setNumBins(int n);

    // =========================================================================
    // Main entry point
    // =========================================================================

    /**
     * @brief Ingest one DataPoint and run all detection logic.
     *
     * This is the single entry point for new data.  The function:
     *   1. Computes distDeg and azimuthDeg relative to the current base.
     *   2. Appends the point to the appropriate mode store (m_all, m_ship,
     *      m_srv, m_foot).
     *   3. Emits pointAdded() so the scatter map can update.
     *   4. Indexes the point into the azimuth-bucket maps.
     *   5. For SRV and OnFoot: indexes into heading-bucket maps and returns.
     *   6. For Ship: runs the full heading-bucket + direction-change state
     *      machine (emits newHeadingDetected, activeLegUpdated, legFinalized
     *      as appropriate).
     *
     * @param pt  The raw point as received from CsvTailReader (distDeg and
     *            azimuthDeg will be overwritten here).
     */
    void addPoint(const DataPoint& pt);

    // =========================================================================
    // Read-only accessors
    // =========================================================================

    /** @brief All points ever added, in arrival order. */
    const QVector<DataPoint>& allPoints()   const { return m_all; }

    /** @brief Ship-mode points only, in arrival order. */
    const QVector<DataPoint>& shipPoints()  const { return m_ship; }

    /** @brief SRV-mode points only, in arrival order. */
    const QVector<DataPoint>& srvPoints()   const { return m_srv; }

    /** @brief OnFoot-mode points only, in arrival order. */
    const QVector<DataPoint>& footPoints()  const { return m_foot; }

    /**
     * @brief Return all completed (finalised) legs for a given heading bucket.
     *
     * If the active leg belongs to the same bucket, it is appended to the
     * result so the profile plot always shows the most up-to-date data.
     *
     * @param headingBucket  Bucket value as returned by headingBucket().
     * @return               Ordered vector of Leg objects (may be empty).
     */
    QVector<Leg> legsForHeading(int headingBucket) const;

    /**
     * @brief Return SRV or OnFoot points that fall in a given heading bucket.
     *
     * The bucket is the ship heading bucket at the time those points were
     * recorded (i.e. the heading the ship was flying, not the geometric
     * bearing from base).
     *
     * @param bucket  Heading bucket (rounded heading value).
     * @param mode    FlightMode::SRV or FlightMode::OnFoot only.
     * @return        Vector of matching DataPoints (may be empty).
     */
    QVector<DataPoint> pointsForHeading(int bucket, FlightMode mode) const;

    /**
     * @brief Return the active (in-progress) leg.
     *
     * The active leg is the one currently being extended by incoming ship
     * points.  It is not yet in legsForHeading() (except through the special
     * inclusion logic there) and will be finalised on the next heading-bucket
     * change or confirmed direction reversal.
     */
    const Leg& activeLeg() const { return m_activeLeg; }

    /**
     * @brief Return all heading buckets that have been seen, sorted ascending.
     *
     * Used to populate the heading list widget in the UI.
     */
    QVector<int> knownHeadings() const;

    /**
     * @brief Return points for a given azimuth bucket.
     *
     * Points are indexed by the geometric bearing from the base at the time
     * they were recorded (DataPoint::azimuthDeg rounded to the nearest bucket).
     * This is different from the heading bucket, which is based on the compass.
     *
     * @param bucket  Azimuth bucket in degrees (output of headingBucket() on
     *                the rounded azimuthDeg).
     * @param mode    FlightMode::Ship, ::SRV, or ::OnFoot.
     * @return        Matching DataPoints in arrival order (may be empty).
     */
    QVector<DataPoint> pointsByAzimuth(int bucket, FlightMode mode) const;

    // =========================================================================
    // Geometry helpers — also used externally
    // =========================================================================

    /**
     * @brief Round a raw compass heading to the nearest bin centre.
     *
     * ### Algorithm
     * With N bins the bin width is W = 360 / N degrees.  A heading h is
     * mapped to the nearest multiple of W by:
     * @code
     *   bucket = round(h / W) * W  mod 360
     * @endcode
     * The initial `((h % 360) + 360) % 360` normalises h into [0, 360) to
     * handle negative inputs and values ≥ 360.
     *
     * Example (N=72, W=5°):
     *   - h=177° → round(177/5)*5 = round(35.4)*5 = 35*5 = 175°
     *   - h=178° → round(178/5)*5 = round(35.6)*5 = 36*5 = 180°
     *
     * @param heading  Raw compass heading in degrees (any integer, may be
     *                 outside [0, 360)).
     * @return         Bucket value in [0, 360), always a multiple of W.
     */
    int    headingBucket(int heading) const;

    /**
     * @brief Compute angular distance from a base position in degrees of arc.
     *
     * ### Algorithm — equirectangular approximation
     * A naive Euclidean distance in lat/lon space would be wrong because one
     * degree of longitude is shorter (in metres) than one degree of latitude
     * at any non-equatorial latitude.  The equirectangular correction scales
     * the longitude difference by cos(baseLat):
     *
     * @code
     *   dlat = lat  - baseLat                     [°]
     *   dlon = (lon - baseLon) * cos(baseLat·π/180) [°, East-West corrected]
     *   dist = sqrt(dlat² + dlon²)                [° of arc]
     * @endcode
     *
     * The result is in degrees of arc on the planetary surface, independent
     * of the planet's radius.  This is intentional: since we do not know the
     * radius from the CSV data, using kilometres would require a hardcoded
     * guess.
     *
     * Accuracy: the approximation is exact at the base latitude and degrades
     * slightly for points far from the base in latitude.  For typical survey
     * extents (< 5° of arc) the error is negligible.
     *
     * @param lat      Latitude of the point.
     * @param lon      Longitude of the point.
     * @param baseLat  Latitude of the reference (base) position.
     * @param baseLon  Longitude of the reference (base) position.
     * @return         Angular distance in degrees of arc (≥ 0).
     */
    static double distDeg(double lat, double lon,
                          double baseLat, double baseLon);

    /**
     * @brief Compute geometric bearing from the base to a point, [0, 360°).
     *
     * ### Algorithm
     * Using the same East-West correction as distDeg():
     * @code
     *   dlat = lat  - baseLat
     *   dlon = (lon - baseLon) * cos(baseLat·π/180)
     *   az   = atan2(dlon, dlat) · (180/π)   → range [−180, 180]
     *   if az < 0:  az += 360                 → range [0, 360)
     * @endcode
     *
     * Note: atan2(y, x) with y = dlon_corrected and x = dlat gives the angle
     * measured clockwise from North, matching the compass convention.
     * (Standard maths atan2 measures counter-clockwise from East, so the
     * arguments are swapped here.)
     *
     * @param lat      Latitude of the point.
     * @param lon      Longitude of the point.
     * @param baseLat  Latitude of the reference position.
     * @param baseLon  Longitude of the reference position.
     * @return         Bearing in degrees [0, 360).  0° = North, 90° = East.
     */
    static double azimuthDeg(double lat, double lon,
                             double baseLat, double baseLon);

signals:
    /**
     * @brief Emitted for every successfully added DataPoint (all modes).
     *
     * Connected to ScatterWidget::addPoint() so the 2D map always stays live.
     */
    void pointAdded(const DataPoint& pt);

    /**
     * @brief Emitted whenever the active Ship leg gains a new point.
     *
     * Connected to MainWindow::onActiveLegUpdated(), which forwards the active
     * leg to ProfileWidget::updateActiveLeg() so the live curve redraws.
     */
    void activeLegUpdated();

    /**
     * @brief Emitted when an active Ship leg is closed and stored.
     *
     * The @p headingBucket parameter identifies which bucket's leg list
     * grew.  MainWindow uses this to refresh the profile plot if the
     * affected bucket is currently selected.
     *
     * @param headingBucket  The bucket whose leg was just finalised.
     */
    void legFinalized(int headingBucket);

    /**
     * @brief Emitted the first time a new heading bucket is encountered.
     *
     * MainWindow adds an entry to the heading list widget in response.
     *
     * @param headingBucket  The new bucket value.
     */
    void newHeadingDetected(int headingBucket);

private:
    /** @brief Close the active leg and store it in m_legs. */
    void        finalizeActiveLeg();

    /**
     * @brief Open a new active leg starting with @p pt.
     * @param bucket  Heading bucket for the new leg.
     * @param dir     Direction (Forward / Backward) determined at this moment.
     * @param pt      First point of the new leg.
     */
    void        startNewLeg(int bucket, LegDir dir, const DataPoint& pt);

    /**
     * @brief Determine travel direction from the current distDeg ring buffer.
     *
     * Runs a least-squares linear regression over the last DIR_WINDOW
     * distance samples.  The slope of the best-fit line is the
     * estimated rate of change of distDeg per sample interval.
     *
     * @return LegDir::Forward if slope > +threshold,
     *         LegDir::Backward if slope < −threshold,
     *         LegDir::Unknown otherwise.
     */
    LegDir      computeDirection() const;

    // ── Base position ─────────────────────────────────────────────────────────
    double m_baseLat =  27.8797;  ///< Latitude of the survey base.
    double m_baseLon = -35.5017;  ///< Longitude of the survey base.

    // ── Raw point stores ──────────────────────────────────────────────────────
    QVector<DataPoint> m_all;   ///< Every accepted point, all modes.
    QVector<DataPoint> m_ship;  ///< Ship-mode points only.
    QVector<DataPoint> m_srv;   ///< SRV-mode points only.
    QVector<DataPoint> m_foot;  ///< OnFoot-mode points only.

    // ── Binning configuration ─────────────────────────────────────────────────
    /**
     * @brief Number of heading bins.  Bin width = 360 / m_numBins degrees.
     * Default 72 → 5°/bin.
     */
    int m_numBins = 72;

    // ── Leg storage, indexed by heading bucket ────────────────────────────────
    /** @brief All finalised legs, keyed by heading bucket value. */
    QMap<int, QVector<Leg>> m_legs;

    /** @brief SRV points indexed by ship heading bucket (for profile queries). */
    QMap<int, QVector<DataPoint>> m_srvByHeading;

    /** @brief OnFoot points indexed by ship heading bucket. */
    QMap<int, QVector<DataPoint>> m_footByHeading;

    // ── Azimuth-bucket stores (all modes, keyed by geometric bearing bucket) ──
    /** @brief Ship points indexed by azimuth bucket (geometric bearing). */
    QMap<int, QVector<DataPoint>> m_shipByAzimuth;

    /** @brief SRV points indexed by azimuth bucket. */
    QMap<int, QVector<DataPoint>> m_srvByAzimuth;

    /** @brief OnFoot points indexed by azimuth bucket. */
    QMap<int, QVector<DataPoint>> m_footByAzimuth;

    // ── Leg-detection state ───────────────────────────────────────────────────
    /** @brief The in-progress leg currently being extended. */
    Leg    m_activeLeg;

    /** @brief True once the first leg has been started (enough data to determine direction). */
    bool   m_legStarted = false;

    /**
     * @brief Ring buffer of the last DIR_WINDOW distDeg values.
     *
     * Older values are removed from the front when the buffer reaches
     * capacity.  Used exclusively by computeDirection().
     */
    QVector<double> m_recentDists;

    /**
     * @brief Number of consecutive samples that agreed on a direction change.
     *
     * Direction changes are only accepted after DIR_CONFIRM consecutive
     * samples all disagree with the current leg's direction.  This counter
     * is reset to zero whenever a sample agrees with the current direction,
     * or whenever a new leg is started.
     */
    int m_dirChangeCount = 0;

    // ── Compile-time constants ────────────────────────────────────────────────

    /**
     * @brief Size of the sliding window for direction regression, in samples.
     *
     * 4 samples is a good trade-off: short enough to react within ~2 seconds
     * of a reversal (assuming 0.5 Hz logging), long enough to suppress noise.
     */
    static constexpr int DIR_WINDOW = 4;

    /**
     * @brief Number of consecutive confirming samples before a direction
     *        change is accepted.
     *
     * Prevents false splits caused by brief GPS jitter near a turning point.
     */
    static constexpr int DIR_CONFIRM = 3;

    /** @brief Set of every heading bucket seen, used to track newHeadingDetected(). */
    QSet<int> m_seenHeadings;
};
