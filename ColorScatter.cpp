/**
 * @file ColorScatter.cpp
 * @brief Implementation of ColorScatterPlottable — Plasma colourmap, per-point
 *        rendering, and QCPAbstractPlottable interface.
 */
#include "ColorScatter.h"
#include <cmath>
#include <algorithm>
#include <QPainterPath>


// =============================================================================
// Plasma colourmap — 16 control points
// =============================================================================

/**
 * @brief Lookup table of 16 RGB control points from Matplotlib's "plasma" map.
 *
 * The full plasma colourmap has 256 entries; these 16 were evenly sampled
 * at indices 0, 17, 34, …, 255.  Linear interpolation between consecutive
 * entries in plasmaColor() gives a visually smooth result indistinguishable
 * from the full table for the point sizes used in this application.
 *
 * Colour progression (t = 0 → 1):
 *   deep violet → purple → magenta → red-orange → yellow → green-teal
 */
static const struct { int r, g, b; } PLASMA[16] = {
    {  13,   8, 135 },  // t=0/15  deep violet  (#0D0887)
    {  75,   3, 161 },  // t=1/15
    { 125,   3, 168 },  // t=2/15
    { 168,  11, 159 },  // t=3/15
    { 204,  50, 126 },  // t=4/15
    { 227,  86,  97 },  // t=5/15
    { 244, 121,  67 },  // t=6/15
    { 252, 155,  42 },  // t=7/15
    { 253, 188,  24 },  // t=8/15
    { 248, 220,  16 },  // t=9/15
    { 240, 249,  33 },  // t=10/15
    { 210, 249,  93 },  // t=11/15
    { 176, 240, 131 },  // t=12/15
    { 139, 225, 160 },  // t=13/15
    { 100, 205, 182 },  // t=14/15
    {  68, 187, 196 },  // t=15/15 teal (#44BBC4)
};

/**
 * @brief Interpolate between the 16 Plasma control points for t ∈ [0, 1].
 *
 * ### Algorithm
 * 1. Clamp t to [0, 1].
 * 2. Scale to the table index space: pos = t × 15.0.
 * 3. Find the lower index lo = floor(pos) and upper index hi = lo + 1.
 * 4. Compute fractional part f = pos − lo.
 * 5. Linearly interpolate each channel:
 *    @code
 *      channel = PLASMA[lo].channel + f × (PLASMA[hi].channel − PLASMA[lo].channel)
 *    @endcode
 *
 * @param t  Normalised value in [0, 1].
 * @return   Interpolated RGB colour.
 */
QColor ColorScatterPlottable::plasmaColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    double  pos = t * 15.0;           // map to [0, 15]
    int     lo  = static_cast<int>(std::floor(pos));
    int     hi  = std::min(lo + 1, 15);
    double  f   = pos - lo;           // fractional part for interpolation

    return QColor(
        static_cast<int>(PLASMA[lo].r + f * (PLASMA[hi].r - PLASMA[lo].r)),
        static_cast<int>(PLASMA[lo].g + f * (PLASMA[hi].g - PLASMA[lo].g)),
        static_cast<int>(PLASMA[lo].b + f * (PLASMA[hi].b - PLASMA[lo].b))
    );
}


// =============================================================================
// Shape size helper
// =============================================================================

/**
 * @brief Return the pixel size for a given FlightMode.
 *
 * Ship is the smallest to keep the map uncluttered when many ship points
 * are present.  SRV and OnFoot are progressively larger so they stand out.
 */
int ColorScatterPlottable::sizeForMode(FlightMode m, int base)
{
    switch (m) {
    case FlightMode::Ship:   return base;
    case FlightMode::SRV:    return base + 2;
    case FlightMode::OnFoot: return base + 4;
    default:                 return base;
    }
}


// =============================================================================
// Constructor
// =============================================================================

/**
 * @brief Register with the parent plot; disable pen/brush at the plottable level.
 *
 * Each point is painted individually in draw(), so no plottable-level pen or
 * brush is needed.  setPen(Qt::NoPen) avoids QCustomPlot drawing an unwanted
 * line between points.
 */
ColorScatterPlottable::ColorScatterPlottable(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
{
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);
}


// =============================================================================
// Data management
// =============================================================================

/**
 * @brief Append a point and optionally extend the colour range.
 *
 * When m_autoRange is true, minVal/maxVal are only ever expanded (never
 * contracted) to preserve the range for all previously plotted points.
 * A call to plot->replot() after this method will redraw all points
 * including any whose colour has changed due to the range extension.
 */
void ColorScatterPlottable::addPoint(double x, double y, double value, FlightMode mode)
{
    m_data.append({ x, y, value, mode });

    if (m_autoRange) {
        if (value < m_minVal) m_minVal = value;
        if (value > m_maxVal) m_maxVal = value;
    }
}

/**
 * @brief Convenience wrapper: use lon / lat from the DataPoint.
 */
void ColorScatterPlottable::addPoint(const DataPoint& pt, double value)
{
    addPoint(pt.lon, pt.lat, value, pt.mode);
}

/**
 * @brief Clear all data points.
 *
 * When auto-range is active, the range sentinels are reset to ±1e9 so that
 * the first point added afterwards correctly initialises the range.  When the
 * range is fixed (m_autoRange = false), the existing min/max are kept so that
 * a rebuildAll() can re-add data without losing the user-set scale.
 */
void ColorScatterPlottable::clearData()
{
    m_data.clear();
    if (m_autoRange) {
        m_minVal =  1e9;
        m_maxVal = -1e9;
    }
}


// =============================================================================
// Colour range management
// =============================================================================

/**
 * @brief Set a fixed colour range and disable auto-expansion.
 */
void ColorScatterPlottable::setRange(double minVal, double maxVal)
{
    m_minVal    = minVal;
    m_maxVal    = maxVal;
    m_autoRange = false;
}


// =============================================================================
// Colour computation
// =============================================================================

/**
 * @brief Normalise @p value and call plasmaColor().
 *
 * The normalisation formula is:
 * @code
 *   t = (value − minVal) / (maxVal − minVal)
 * @endcode
 * A collapsed range (maxVal − minVal < 1e-9) returns t = 0.5 (the middle of
 * the colourmap) rather than causing a divide-by-zero.
 */
QColor ColorScatterPlottable::colorForValue(double value) const
{
    double range = m_maxVal - m_minVal;
    if (range < 1e-9) return plasmaColor(0.5);
    double t = (value - m_minVal) / range;
    return plasmaColor(t);
}


// =============================================================================
// draw() — main rendering loop
// =============================================================================

/**
 * @brief Render all points for the current replot.
 *
 * ### Performance considerations
 * - Axis-to-pixel coordinate transforms (coordToPixel) are O(1) per call but
 *   have non-trivial overhead; caching the axis pointers outside the loop
 *   avoids repeated virtual dispatch.
 * - The bounds check (px ± sz vs. clip rect) skips points outside the
 *   visible area before the more expensive paint operations.
 * - The clip rect is set once before the loop; Qt clips individual draw calls
 *   against it automatically, but the explicit bounds check allows us to
 *   skip the painter call entirely for invisible points.
 */
void ColorScatterPlottable::draw(QCPPainter* painter)
{
    if (m_data.isEmpty()) return;

    QCPAxis* kAx = keyAxis();    // horizontal (longitude)
    QCPAxis* vAx = valueAxis();  // vertical   (latitude)

    const QRect clip = clipRect();
    painter->setClipRect(clip, Qt::IntersectClip);

    for (const auto& pt : std::as_const(m_data)) {
        // Map data coordinates → pixel coordinates
        double  px = kAx->coordToPixel(pt.x);
        double  py = vAx->coordToPixel(pt.y);

        // Quick bounds check: skip points whose bounding box is outside the clip
        int sz = sizeForMode(pt.mode, m_baseSize);
        if (px + sz < clip.left()   || px - sz > clip.right()  ||
            py + sz < clip.top()    || py - sz > clip.bottom())
            continue;

        QColor col = colorForValue(pt.value);
        drawSinglePoint(painter, QPointF(px, py), col, pt.mode, sz);
    }

    painter->setClipping(false);
}


// =============================================================================
// drawSinglePoint — paint one symbol
// =============================================================================

/**
 * @brief Paint one coloured symbol at the given screen position.
 *
 * The half-size h = size / 2.0 is used as the radius / half-side length.
 *
 * ### Diamond geometry (OnFoot)
 * The four vertices of an axis-aligned square rotated 45° are:
 * @code
 *   top    = (cx,     cy - h)
 *   right  = (cx + h, cy    )
 *   bottom = (cx,     cy + h)
 *   left   = (cx - h, cy    )
 * @endcode
 * Connected in order by QPainterPath::lineTo() and closed with closeSubpath().
 */
void ColorScatterPlottable::drawSinglePoint(QCPPainter*    painter,
                                            const QPointF& pos,
                                            const QColor&  col,
                                            FlightMode     mode,
                                            int            size) const
{
    const double h = size * 0.5;

    painter->setPen(QPen(col.darker(140), 0.5));  // thin dark border
    painter->setBrush(QBrush(col));

    switch (mode)
    {
    case FlightMode::Ship:
        painter->drawEllipse(pos, h, h);
        break;

    case FlightMode::SRV:
        painter->drawRect(QRectF(pos.x() - h, pos.y() - h, size, size));
        break;

    case FlightMode::OnFoot: {
        QPainterPath diamond;
        diamond.moveTo(pos.x(),         pos.y() - h);  // top vertex
        diamond.lineTo(pos.x() + h,     pos.y());       // right vertex
        diamond.lineTo(pos.x(),         pos.y() + h);  // bottom vertex
        diamond.lineTo(pos.x() - h,     pos.y());       // left vertex
        diamond.closeSubpath();
        painter->drawPath(diamond);
        break;
    }

    default:
        painter->drawEllipse(pos, h, h);
        break;
    }
}


// =============================================================================
// drawLegendIcon
// =============================================================================

/**
 * @brief Draw a compact legend icon suggesting "colour-coded scatter".
 *
 * A Plasma gradient strip occupies the middle 40% of the icon height; a small
 * white disk is centred on top of it.  9 colour stops (i/8 for i in 0..8)
 * are used to faithfully represent the full gradient within the icon width.
 */
void ColorScatterPlottable::drawLegendIcon(QCPPainter* painter,
                                           const QRectF& rect) const
{
    // Gradient strip
    QLinearGradient grad(rect.left(), 0, rect.right(), 0);
    for (int i = 0; i <= 8; ++i)
        grad.setColorAt(i / 8.0, plasmaColor(i / 8.0));

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(grad));
    QRectF strip = rect.adjusted(0, rect.height() * 0.3, 0, -rect.height() * 0.3);
    painter->drawRect(strip);

    // Central disk
    QPointF center = rect.center();
    double  r = rect.height() * 0.25;
    painter->setBrush(QBrush(Qt::white));
    painter->setPen(QPen(Qt::black, 0.5));
    painter->drawEllipse(center, r, r);
}


// =============================================================================
// selectTest — nearest-point distance
// =============================================================================

/**
 * @brief Return the pixel distance from @p pos to the nearest stored point.
 *
 * O(N) scan; suitable because QCustomPlot only calls this during mouse events,
 * not on every replot.
 *
 * @return Minimum pixel distance, or −1 if the plottable is not selectable.
 */
double ColorScatterPlottable::selectTest(const QPointF& pos,
                                         bool           onlySelectable,
                                         QVariant*      details) const
{
    Q_UNUSED(details);
    if (onlySelectable && !selectable()) return -1;
    if (m_data.isEmpty()) return -1;

    QCPAxis* kAx = keyAxis();
    QCPAxis* vAx = valueAxis();

    double minDist = std::numeric_limits<double>::max();
    for (const auto& pt : std::as_const(m_data)) {
        double dx = kAx->coordToPixel(pt.x) - pos.x();
        double dy = vAx->coordToPixel(pt.y) - pos.y();
        double d  = std::sqrt(dx*dx + dy*dy);
        if (d < minDist) minDist = d;
    }
    return minDist;
}


// =============================================================================
// getKeyRange / getValueRange — used by rescaleAxes()
// =============================================================================

/**
 * @brief Return the [min, max] range of x (longitude) values.
 *
 * @p inSignDomain allows QCustomPlot to request only positive or negative
 * values (for log-scale axes).  With linear axes (our case) it is always
 * QCP::sdBoth and the filter has no effect.
 */
QCPRange ColorScatterPlottable::getKeyRange(bool& foundRange,
                                             QCP::SignDomain inSignDomain) const
{
    QCPRange range;
    foundRange = false;
    for (const auto& pt : std::as_const(m_data)) {
        if (inSignDomain == QCP::sdNegative && pt.x >= 0) continue;
        if (inSignDomain == QCP::sdPositive && pt.x <= 0) continue;
        if (!foundRange) {
            range.lower = range.upper = pt.x;
            foundRange = true;
        } else {
            if (pt.x < range.lower) range.lower = pt.x;
            if (pt.x > range.upper) range.upper = pt.x;
        }
    }
    return range;
}

/**
 * @brief Return the [min, max] range of y (latitude) values.
 *
 * @p keyRange is ignored (not a function-of-key plottable).
 */
QCPRange ColorScatterPlottable::getValueRange(bool& foundRange,
                                               QCP::SignDomain inSignDomain,
                                               const QCPRange& keyRange) const
{
    Q_UNUSED(keyRange);
    QCPRange range;
    foundRange = false;
    for (const auto& pt : std::as_const(m_data)) {
        if (inSignDomain == QCP::sdNegative && pt.y >= 0) continue;
        if (inSignDomain == QCP::sdPositive && pt.y <= 0) continue;
        if (!foundRange) {
            range.lower = range.upper = pt.y;
            foundRange = true;
        } else {
            if (pt.y < range.lower) range.lower = pt.y;
            if (pt.y > range.upper) range.upper = pt.y;
        }
    }
    return range;
}
