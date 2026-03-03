/**
 * @file RaycastBoundaryWidget.h
 * @brief Floating window that maps positions where altRaycast transitions
 *        from true to false (raycast altitude → radius-mode altitude).
 *
 * ### What is shown
 * Each time a DataPoint arrives with altRaycast == false immediately after
 * a point with altRaycast == true (for the same FlightMode), the geographic
 * position (lon, lat) is recorded and drawn as an upward-pointing orange
 * triangle.
 *
 * A yellow cross marks the running centroid of all such positions.
 *
 * ### Hover tooltips
 * - Hovering a transition triangle shows: timestamp, lat, lon, altitude.
 * - Hovering the centroid cross shows: lat, lon, and point count.
 *
 * ### Why per-mode tracking
 * Ship, SRV and OnFoot points may be interleaved in the stream.  Tracking
 * the previous altRaycast state independently for each mode avoids spurious
 * transitions caused by a mode switch.
 */
#pragma once
#include "DataPoint.h"
#include <QWidget>
#include <QMap>
#include <QVector>

class QCustomPlot;
class QCPGraph;
class QCPItemText;
class QMouseEvent;


class RaycastBoundaryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RaycastBoundaryWidget(QWidget* parent = nullptr);

    /** @brief Move the ★ BASE marker to (lon, lat). */
    void setBase(double lat, double lon);

public slots:
    /**
     * @brief Inspect an incoming point and record a transition if present.
     *
     * A transition is detected when pt.altRaycast == false and the previous
     * altRaycast value for pt.mode was true.  The point's (lon, lat) is then
     * added to the scatter and the centroid is updated.
     */
    void onNewPoint(const DataPoint& pt);

private slots:
    /** @brief Show a hover tooltip for the nearest transition point or centroid. */
    void onMouseMove(QMouseEvent* event);

private:
    void setupPlot();

    /** @brief Recompute and redraw the centroid marker. */
    void updateCentroid();

    // ── QCustomPlot objects ───────────────────────────────────────────────────
    QCustomPlot*  m_plot          = nullptr;
    QCPGraph*     m_transGraph    = nullptr;  ///< Orange triangles at ray→0 positions.
    QCPGraph*     m_centroidGraph = nullptr;  ///< Yellow cross at the centroid.
    QCPItemText*  m_baseMarker    = nullptr;  ///< "★ BASE" text item.

    // ── Per-mode raycast state ────────────────────────────────────────────────
    QMap<int, bool> m_prevRaycast;

    // ── Transition point store (for hover lookup) ─────────────────────────────
    QVector<DataPoint> m_transPoints;

    // ── Centroid accumulation ─────────────────────────────────────────────────
    double m_sumLon      = 0.0;
    double m_sumLat      = 0.0;
    int    m_count       = 0;
    double m_centroidLon = 0.0;  ///< Current centroid longitude (for hover).
    double m_centroidLat = 0.0;  ///< Current centroid latitude  (for hover).

    // ── Axis range tracking ───────────────────────────────────────────────────
    double m_lonMin =  1e9;  ///< Running minimum longitude of transition points.
    double m_lonMax = -1e9;  ///< Running maximum longitude of transition points.
    double m_latMin =  1e9;  ///< Running minimum latitude of transition points.
    double m_latMax = -1e9;  ///< Running maximum latitude of transition points.
};
