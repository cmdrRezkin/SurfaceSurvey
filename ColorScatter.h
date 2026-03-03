/**
 * @file ColorScatter.h
 * @brief Custom QCustomPlot plottable that renders a colour-coded scatter map.
 *
 * This file defines two types:
 *   - ColorScatterPoint — lightweight POD storage for one plotted sample.
 *   - ColorScatterPlottable — a QCPAbstractPlottable subclass that draws each
 *     point with a shape (disk / square / diamond) determined by FlightMode
 *     and a colour determined by a scalar value mapped through the Plasma
 *     colourmap.
 */
#pragma once
#include "DataPoint.h"
#include "qcustomplot.h"
#include <QVector>
#include <QColor>


// =============================================================================
// ColorScatterPoint — raw storage for one plotted sample
// =============================================================================

/**
 * @brief Stores the coordinates and scalar value of one survey point.
 *
 * ### Design note: colour is NOT stored here
 * Colours are computed at draw() time from the current [minVal, maxVal] range.
 * This means the entire scatter can be recoloured (e.g. when the user switches
 * from "Altitude" to "Gravity" or enables a fixed range) by simply calling
 * plot->replot() — no data rebuild is required.
 */
struct ColorScatterPoint
{
    double     x;       ///< Horizontal coordinate (longitude on the scatter map).
    double     y;       ///< Vertical coordinate (latitude on the scatter map).
    double     value;   ///< Scalar that drives the colour: altitude or gravity.
    FlightMode mode;    ///< Determines the rendered shape (disk / square / diamond).
};


// =============================================================================
// ColorScatterPlottable
// =============================================================================

/**
 * @brief QCustomPlot plottable rendering colour-coded survey points.
 *
 * ### Shapes (by FlightMode)
 * | FlightMode | Shape            | Size relative to base |
 * |------------|------------------|-----------------------|
 * | Ship       | ● filled disk    | base                  |
 * | SRV        | ■ filled square  | base + 2 px           |
 * | OnFoot     | ◆ filled diamond | base + 4 px           |
 *
 * Larger shapes for SRV and OnFoot ensure they remain visible when overlaid
 * with ship points.
 *
 * ### Colour mapping — Plasma colourmap
 * The scalar value (altitude or gravity) is mapped to [0, 1] linearly:
 * @code
 *   t = (value - minVal) / (maxVal - minVal)
 * @endcode
 * and then passed to plasmaColor(t), which linearly interpolates between 16
 * control points sampled from Matplotlib's "plasma" colourmap.  Plasma was
 * chosen because it is perceptually uniform, colourblind-friendly, and looks
 * good on dark backgrounds.
 *
 * ### Auto-range vs fixed range
 * When m_autoRange is true (default), minVal and maxVal expand automatically
 * as new points arrive.  The user can freeze the range (setRange()) to compare
 * multiple datasets on the same colour scale.
 *
 * ### Performance
 * The draw() method maps data coordinates to pixel coordinates and skips
 * points outside the visible axis rect before painting.  This is an O(N)
 * operation but fast because the pixel transform is a simple linear mapping.
 *
 * @note Not thread-safe.  All methods must be called from the Qt GUI thread.
 */
class ColorScatterPlottable : public QCPAbstractPlottable
{
    Q_OBJECT

public:
    /**
     * @brief Construct and attach to the given axes.
     *
     * The plottable registers itself with the parent QCustomPlot automatically
     * (via QCPAbstractPlottable's constructor).  No pen or brush is set at the
     * plottable level — each point is painted individually in draw().
     *
     * @param keyAxis    Horizontal axis (longitude).
     * @param valueAxis  Vertical axis   (latitude).
     */
    explicit ColorScatterPlottable(QCPAxis* keyAxis, QCPAxis* valueAxis);

    // =========================================================================
    // Data management
    // =========================================================================

    /**
     * @brief Append one point by raw coordinates.
     *
     * If m_autoRange is true, minVal and maxVal are extended if @p value falls
     * outside the current range.
     *
     * @param x      Horizontal coordinate (longitude).
     * @param y      Vertical coordinate   (latitude).
     * @param value  Scalar for colour mapping (altitude or gravity).
     * @param mode   Flight mode — determines rendered shape.
     */
    void addPoint(double x, double y, double value, FlightMode mode);

    /**
     * @brief Convenience overload: extract coordinates and mode from a DataPoint.
     *
     * Uses pt.lon as x, pt.lat as y, and pt.mode for shape selection.
     * The caller supplies the already-computed scalar value.
     *
     * @param pt     Source DataPoint.
     * @param value  Scalar for colour mapping.
     */
    void addPoint(const DataPoint& pt, double value);

    /**
     * @brief Remove all stored points and reset the auto-range to empty.
     *
     * If m_autoRange is false (fixed range), the range is NOT reset — only the
     * point list is cleared.
     */
    void clearData();

    /** @brief Return the number of stored points. */
    int  pointCount() const { return m_data.size(); }

    // =========================================================================
    // Colour range
    // =========================================================================

    /**
     * @brief Fix the colour range to [minVal, maxVal] and disable auto-expand.
     * @param minVal  Lower bound of the colour range.
     * @param maxVal  Upper bound of the colour range.
     */
    void   setRange(double minVal, double maxVal);

    /**
     * @brief Re-enable (or disable) automatic range expansion on addPoint().
     * @param on  true = auto-range; false = keep the current fixed range.
     */
    void   setAutoRange(bool on) { m_autoRange = on; }

    /** @brief Current lower bound of the colour range. */
    double minVal() const { return m_minVal; }

    /** @brief Current upper bound of the colour range. */
    double maxVal() const { return m_maxVal; }

    // =========================================================================
    // Appearance
    // =========================================================================

    /**
     * @brief Set the base pixel size used for Ship-mode points.
     *
     * SRV points are drawn at (base + 2) px and OnFoot at (base + 4) px.
     * Default: 5 px.
     *
     * @param px  Desired pixel size for Ship points.
     */
    void setBaseSize(int px) { m_baseSize = px; }

    /** @brief Return the current Ship-mode pixel size. */
    int  baseSize()  const   { return m_baseSize; }

    // =========================================================================
    // Static utilities (also used by legend widgets)
    // =========================================================================

    /**
     * @brief Map a normalised scalar t ∈ [0, 1] to a Plasma colourmap colour.
     *
     * ### Plasma colourmap
     * 16 control points are sampled from Matplotlib's "plasma" colourmap
     * (a perceptually uniform, colourblind-safe palette):
     *   - t = 0.00: deep violet (#0D0887) → cold / low value
     *   - t ≈ 0.50: bright magenta-orange → middle range
     *   - t = 1.00: teal (#44BBC4)        → hot / high value
     *
     * Linear interpolation between consecutive control points gives a smooth
     * colour transition.  Values outside [0, 1] are clamped.
     *
     * @param t  Normalised value in [0, 1].
     * @return   Interpolated RGB colour.
     */
    static QColor  plasmaColor(double t);

    /**
     * @brief Return the pixel diameter for a given FlightMode.
     *
     * | Mode   | Size          |
     * |--------|---------------|
     * | Ship   | base          |
     * | SRV    | base + 2      |
     * | OnFoot | base + 4      |
     * | other  | base          |
     *
     * @param m     Flight mode.
     * @param base  Reference size for Ship.
     * @return      Pixel diameter for this mode.
     */
    static int     sizeForMode(FlightMode m, int base);

    // =========================================================================
    // QCPAbstractPlottable interface — required overrides
    // =========================================================================

    /**
     * @brief Return the pixel distance from @p pos to the nearest data point.
     *
     * Used by QCustomPlot's selection mechanism.  Implementation is an O(N)
     * linear scan over all stored points.
     */
    double   selectTest(const QPointF& pos,
                        bool onlySelectable,
                        QVariant* details = nullptr) const override;

    /**
     * @brief Return the bounding range of all x (longitude) values.
     *
     * Called by QCustomPlot::rescaleAxes() to auto-fit the horizontal view.
     */
    QCPRange getKeyRange(bool& foundRange,
                         QCP::SignDomain inSignDomain) const override;

    /**
     * @brief Return the bounding range of all y (latitude) values.
     *
     * Called by QCustomPlot::rescaleAxes() to auto-fit the vertical view.
     */
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain,
                           const QCPRange& keyRange = QCPRange()) const override;

protected:
    /**
     * @brief Render all stored points.
     *
     * Called by QCustomPlot on every replot().  For each point:
     *   1. Map (lon, lat) → pixel coordinates (px, py).
     *   2. Skip points whose bounding box lies entirely outside the clip rect.
     *   3. Compute colour from the current range via colorForValue().
     *   4. Dispatch to drawSinglePoint() for painting.
     */
    void draw(QCPPainter* painter) override;

    /**
     * @brief Draw the legend icon — a Plasma gradient strip with a white disk.
     */
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

private:
    /**
     * @brief Paint one symbol at screen position @p screenPos.
     *
     * | Mode   | Shape   | How it is drawn                              |
     * |--------|---------|----------------------------------------------|
     * | Ship   | disk    | painter->drawEllipse (radius h = size/2)     |
     * | SRV    | square  | painter->drawRect    (side = size)           |
     * | OnFoot | diamond | QPainterPath with 4 vertices (top/right/     |
     * |        |         |   bottom/left) at distance h from centre     |
     *
     * A darkened border (col.darker(140)) is drawn at 0.5 px width to keep
     * adjacent points visually distinct.
     *
     * @param painter    Active QPainter.
     * @param screenPos  Centre in pixel coordinates.
     * @param col        Fill colour (from colorForValue).
     * @param mode       Determines shape.
     * @param size       Pixel diameter of the symbol.
     */
    void   drawSinglePoint(QCPPainter*    painter,
                           const QPointF& screenPos,
                           const QColor&  col,
                           FlightMode     mode,
                           int            size) const;

    /**
     * @brief Map a raw scalar value to a colour using the current range.
     *
     * Normalises @p value with respect to [m_minVal, m_maxVal], then calls
     * plasmaColor().  If the range collapses (< 1e-9), returns the midpoint
     * colour (t = 0.5) to avoid division by zero.
     *
     * @param value  Raw scalar (altitude in m, or gravity in g).
     * @return       Corresponding Plasma colour.
     */
    QColor colorForValue(double value) const;

    // ── Data ──────────────────────────────────────────────────────────────────
    QVector<ColorScatterPoint> m_data;  ///< All stored points in arrival order.

    // ── Colour range ──────────────────────────────────────────────────────────
    double m_minVal    =   0.0;  ///< Lower bound of the colour range.
    double m_maxVal    = 200.0;  ///< Upper bound of the colour range.
    bool   m_autoRange = true;   ///< Whether to auto-extend the range on addPoint().
    int    m_baseSize  =    5;   ///< Base pixel size for Ship-mode symbols.
};
