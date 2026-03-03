/**
 * @file ScatterWidget.h
 * @brief 2D lat/lon scatter map with colour-coded altitude or gravity.
 */
#pragma once
#include "DataPoint.h"
#include "ColorScatter.h"
#include <QWidget>

class QCustomPlot;
class QCPItemText;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMouseEvent;


/**
 * @brief 2D lat/lon map rendered by ColorScatterPlottable.
 *
 * ### What is shown
 * Every incoming DataPoint is plotted at (longitude, latitude).  The point's
 * colour encodes a scalar value chosen by the user via a combo box:
 *   - **Altitude (m)** — default; for OnFoot points, gravity is used instead
 *     since they have no meaningful altitude.
 *   - **Gravity (g)**  — relative surface gravity; only meaningful for OnFoot
 *     records but displayed for all modes (ship/SRV gravity is 0).
 *
 * ### Shape key
 * | Shape     | FlightMode |
 * |-----------|------------|
 * | ● disk    | Ship       |
 * | ■ square  | SRV        |
 * | ◆ diamond | OnFoot     |
 *
 * ### Base marker
 * A "★ BASE" text item is drawn at the survey base coordinates and updated
 * via setBase().
 *
 * ### Controls bar
 * | Widget      | Effect                                               |
 * |-------------|------------------------------------------------------|
 * | Color combo | Switch between Altitude and Gravity colour modes     |
 * | Fix range   | Freeze the colour range at the current min/max       |
 * | Range label | Shows the current [min – max value unit]             |
 *
 * ### Hover tooltip
 * Moving the mouse within the plot area shows a tooltip for the nearest data
 * point (within 18 px).
 *
 * @see ColorScatterPlottable — for the actual rendering logic.
 */
class ScatterWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Construct the widget, create the QCustomPlot and the plottable.
     * @param parent  Owning widget.
     */
    explicit ScatterWidget(QWidget* parent = nullptr);

    /**
     * @brief Update the base marker position.
     *
     * Called by MainWindow::applyBase() whenever the base coordinates are set
     * (either from the CLI --base option or from the first CSV point).
     *
     * @param lat  Latitude of the base in decimal degrees.
     * @param lon  Longitude of the base in decimal degrees.
     */
    void setBase(double lat, double lon);

public slots:
    /**
     * @brief Append one new point to the scatter map.
     *
     * Determines the colour value from the current colour mode, delegates to
     * ColorScatterPlottable::addPoint(), expands the axis ranges if needed,
     * and schedules a queued repaint.
     *
     * @param pt  DataPoint with lat/lon/mode and altitude/gravity fields filled.
     */
    void addPoint(const DataPoint& pt);

    /**
     * @brief Rebuild the entire plot from a new set of points.
     *
     * Used when the colour mode or range setting changes: the plottable is
     * cleared and all stored points are re-added with the new value mapping.
     *
     * @param pts  All DataPoints to display (typically m_allPts).
     */
    void rebuildAll(const QVector<DataPoint>& pts);

private slots:
    /**
     * @brief Called when the user selects a different colour mode in the combo.
     * @param index  0 = Altitude, 1 = Gravity.
     */
    void onColorModeChanged(int index);

    /**
     * @brief Called when the "Fix range" checkbox is toggled.
     *
     * If @p fixed is true, the current min/max are frozen.  If false,
     * auto-range is re-enabled and the plot is rebuilt.
     *
     * @param fixed  true = fix the current range; false = auto.
     */
    void onRangeToggled(bool fixed);

    /**
     * @brief Called when either range spinbox value changes.
     *
     * Applies the new [min, max] to the plottable and redraws.  No full
     * data rebuild is needed because colours are computed at draw() time.
     */
    void onRangeEdited();

    /**
     * @brief Handle mouse movement for hover tooltips.
     *
     * O(N) scan over m_allPts to find the nearest point within 18 px.
     * Shows a QToolTip with mode, position, altitude/gravity, heading, and
     * angular distance from base.
     *
     * @param event  The Qt mouse event (cursor position).
     */
    void onMouseMove(QMouseEvent* event);

private:
    /**
     * @brief Configure the QCustomPlot: dark theme, axis labels, legend,
     *        interactions, and shape-key legend entries.
     */
    void   setupPlot();

    /** @brief (Placeholder) Reserved for a future colour-scale bar widget. */
    void   buildColorBar();

    /**
     * @brief Refresh the range label text from the plottable's current min/max.
     */
    void   updateColorBar();

    /**
     * @brief Return the scalar value that drives the colour for @p pt.
     *
     * Altitude mode: pt.altitude for Ship/SRV, pt.gravity for OnFoot.
     * Gravity mode:  pt.gravity for all modes.
     */
    double valueFor(const DataPoint& pt) const;

    /**
     * @brief Expand axis ranges to include the coordinates of @p pt if needed.
     */
    void   maybeExpandAxes(const DataPoint& pt);

    // ── Widgets ───────────────────────────────────────────────────────────────
    QCustomPlot*              m_plot        = nullptr;  ///< QCustomPlot canvas.
    ColorScatterPlottable*    m_plottable   = nullptr;  ///< Custom plottable.
    QCPItemText*              m_baseMarker  = nullptr;  ///< "★ BASE" label.

    // ── Color scale bar (placeholder — currently unused) ──────────────────────
    QCPAxisRect*  m_colorBarRect  = nullptr;
    QCPColorScale* m_colorScale   = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    int    m_colorMode   = 0;         ///< 0 = Altitude, 1 = Gravity.
    bool   m_fixedRange  = false;     ///< True when the user has frozen the range.

    double m_baseLat     =  27.8797;  ///< Latitude of the base marker.
    double m_baseLon     = -35.5017;  ///< Longitude of the base marker.

    /** @brief All points ever added, kept for rebuildAll() on mode/range change. */
    QVector<DataPoint> m_allPts;

    // ── Axis range tracking ───────────────────────────────────────────────────
    double m_lonMin =  1e9;  ///< Running minimum longitude.
    double m_lonMax = -1e9;  ///< Running maximum longitude.
    double m_latMin =  1e9;  ///< Running minimum latitude.
    double m_latMax = -1e9;  ///< Running maximum latitude.

    QLabel*         m_rangeLabel = nullptr;  ///< Shows "range: min – max unit".
    QDoubleSpinBox* m_minSpin    = nullptr;  ///< Manual lower bound (active when range is fixed).
    QDoubleSpinBox* m_maxSpin    = nullptr;  ///< Manual upper bound (active when range is fixed).
};
