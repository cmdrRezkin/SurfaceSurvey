/**
 * @file ScatterWidget.cpp
 * @brief Implementation of ScatterWidget — 2D lat/lon scatter map.
 */
#include "ScatterWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QToolTip>

// =============================================================================
// Constructor
// =============================================================================

/**
 * @brief Build the UI: controls bar + QCustomPlot + plottable + base marker.
 *
 * ### Layout
 * @code
 *   QVBoxLayout
 *   ├── QHBoxLayout (controls bar)
 *   │   ├── QLabel "Color:"
 *   │   ├── QComboBox (Altitude | Gravity)
 *   │   ├── QCheckBox "Fix range"
 *   │   └── QLabel (range text)
 *   └── QCustomPlot (stretch = 1)
 * @endcode
 */
ScatterWidget::ScatterWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(2);

    // ── Controls bar ─────────────────────────────────────────────────────
    auto* hlay  = new QHBoxLayout;
    hlay->setContentsMargins(4, 2, 4, 2);

    auto* colLabel = new QLabel("Color:", this);
    auto* colCombo = new QComboBox(this);
    colCombo->addItem("Altitude (m)");
    colCombo->addItem("Gravity (g)");
    colCombo->setMaximumWidth(120);

    auto* rangeCheck = new QCheckBox("Fix range", this);

    m_rangeLabel = new QLabel("range: auto", this);
    m_rangeLabel->setStyleSheet("color: #88aaff; font-size: 10px;");

    const QString spinStyle =
        "QDoubleSpinBox { background:#0a0a1a; color:#ccddff; border:1px solid #334;"
        "  padding:1px 3px; max-width:72px; font-size:10px; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:12px; }";

    auto* minLabel = new QLabel("min:", this);
    minLabel->setStyleSheet("color:#88aaff; font-size:10px;");
    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-1e6, 1e6);
    m_minSpin->setDecimals(1);
    m_minSpin->setSingleStep(10.0);
    m_minSpin->setEnabled(false);
    m_minSpin->setStyleSheet(spinStyle);

    auto* maxLabel = new QLabel("max:", this);
    maxLabel->setStyleSheet("color:#88aaff; font-size:10px;");
    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-1e6, 1e6);
    m_maxSpin->setDecimals(1);
    m_maxSpin->setSingleStep(10.0);
    m_maxSpin->setEnabled(false);
    m_maxSpin->setStyleSheet(spinStyle);

    hlay->addWidget(colLabel);
    hlay->addWidget(colCombo);
    hlay->addSpacing(12);
    hlay->addWidget(rangeCheck);
    hlay->addWidget(m_rangeLabel);
    hlay->addSpacing(12);
    hlay->addWidget(minLabel);
    hlay->addWidget(m_minSpin);
    hlay->addSpacing(4);
    hlay->addWidget(maxLabel);
    hlay->addWidget(m_maxSpin);
    hlay->addStretch();
    vlay->addLayout(hlay);

    // ── QCustomPlot ───────────────────────────────────────────────────────
    m_plot = new QCustomPlot(this);
    vlay->addWidget(m_plot, 1);

    setupPlot();

    // Create the custom plottable — it self-registers with the plot
    m_plottable = new ColorScatterPlottable(m_plot->xAxis, m_plot->yAxis);
    m_plottable->setName("Survey points");

    // ── Base marker ───────────────────────────────────────────────────────
    m_baseMarker = new QCPItemText(m_plot);
    m_baseMarker->setText("★ BASE");
    m_baseMarker->setColor(Qt::white);
    m_baseMarker->setFont(QFont("sans-serif", 9, QFont::Bold));
    m_baseMarker->position->setCoords(m_baseLon, m_baseLat);
    m_baseMarker->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);

    connect(colCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScatterWidget::onColorModeChanged);
    connect(rangeCheck, &QCheckBox::toggled,
            this, &ScatterWidget::onRangeToggled);
    connect(m_minSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ScatterWidget::onRangeEdited);
    connect(m_maxSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ScatterWidget::onRangeEdited);
    connect(m_plot, &QCustomPlot::mouseMove,
            this, &ScatterWidget::onMouseMove);
    m_plot->setMouseTracking(true);
}


// =============================================================================
// Plot styling
// =============================================================================

/**
 * @brief Apply the dark theme and create shape-key legend entries.
 *
 * ### Shape-key legend entries
 * Three "invisible" graphs (no data, setVisible(false)) are created solely
 * to populate the legend with the disk/square/diamond shape icons.  They
 * do not affect axis ranges or rendering because they are hidden.
 *
 * Using dedicated invisible graphs is the standard QCustomPlot idiom for
 * adding legend entries that are not backed by actual plottable data.
 */
void ScatterWidget::setupPlot()
{
    m_plot->setBackground(QBrush(QColor(8, 8, 24)));
    m_plot->axisRect()->setBackground(QBrush(QColor(10, 10, 20)));
    m_plot->xAxis->setLabel("Longitude");
    m_plot->yAxis->setLabel("Latitude");

    auto styleAxis = [](QCPAxis* ax) {
        ax->setLabelColor(Qt::white);
        ax->setTickLabelColor(Qt::white);
        ax->setBasePen(QPen(QColor(80, 80, 80)));
        ax->setTickPen(QPen(QColor(80, 80, 80)));
        ax->setSubTickPen(QPen(QColor(50, 50, 50)));
        ax->grid()->setPen(QPen(QColor(35, 35, 55), 1, Qt::DotLine));
    };
    styleAxis(m_plot->xAxis);
    styleAxis(m_plot->yAxis);

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(15, 15, 35, 210)));
    m_plot->legend->setTextColor(Qt::white);
    m_plot->legend->setBorderPen(QPen(QColor(80, 80, 80)));
    m_plot->legend->setFont(QFont("monospace", 8));

    // Legend-only shape entries — hidden from rendering/axis-scaling
    auto addShapeEntry = [&](const QString& label,
                              FlightMode mode,
                              const QColor& col)
    {
        QCPGraph* g = m_plot->addGraph();
        g->setName(label);
        g->setLineStyle(QCPGraph::lsNone);
        QCPScatterStyle ss;
        ss.setShape(mode == FlightMode::Ship   ? QCPScatterStyle::ssDisc
                  : mode == FlightMode::SRV    ? QCPScatterStyle::ssSquare
                                               : QCPScatterStyle::ssDiamond);
        ss.setSize(8);
        ss.setPen(QPen(col.darker(130)));
        ss.setBrush(QBrush(col));
        g->setScatterStyle(ss);
        g->setVisible(false);   // legend-only; does not affect axis ranges
    };

    addShapeEntry("● Ship",    FlightMode::Ship,   QColor(180, 200, 220));
    addShapeEntry("■ SRV",     FlightMode::SRV,    QColor(180, 200, 220));
    addShapeEntry("◆ On foot", FlightMode::OnFoot, QColor(180, 200, 220));
}


// =============================================================================
// Public interface
// =============================================================================

/**
 * @brief Move the ★ BASE marker to (lon, lat) and schedule a repaint.
 */
void ScatterWidget::setBase(double lat, double lon)
{
    m_baseLat = lat;
    m_baseLon = lon;
    if (m_baseMarker)
        m_baseMarker->position->setCoords(lon, lat);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief Add one point and update the display.
 *
 * The value passed to the plottable depends on m_colorMode:
 *   - 0 (Altitude): altitude for Ship/SRV, gravity for OnFoot.
 *   - 1 (Gravity):  gravity for all modes.
 *
 * After adding, the axis ranges are expanded if necessary and a queued
 * replot is scheduled (rpQueuedReplot coalesces multiple rapid updates into
 * a single paint event).
 */
void ScatterWidget::addPoint(const DataPoint& pt)
{
    m_allPts.append(pt);

    double v = valueFor(pt);
    m_plottable->addPoint(pt, v);

    updateColorBar();
    maybeExpandAxes(pt);

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief Clear and rebuild the plottable from the stored point list.
 *
 * When auto-range is active, the range is computed from scratch over all
 * points to ensure the colour scale is correct after a mode switch.
 */
void ScatterWidget::rebuildAll(const QVector<DataPoint>& pts)
{
    m_allPts = pts;
    m_plottable->clearData();

    // Reset axis trackers
    m_lonMin =  1e9;  m_lonMax = -1e9;
    m_latMin =  1e9;  m_latMax = -1e9;

    if (!m_fixedRange) {
        double lo =  1e9, hi = -1e9;
        for (const auto& p : pts) {
            double v = valueFor(p);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (lo <= hi) m_plottable->setRange(lo, hi);
    }

    for (const auto& p : pts) {
        m_plottable->addPoint(p, valueFor(p));
        m_lonMin = qMin(m_lonMin, p.lon);  m_lonMax = qMax(m_lonMax, p.lon);
        m_latMin = qMin(m_latMin, p.lat);  m_latMax = qMax(m_latMax, p.lat);
    }

    if (m_lonMin <= m_lonMax) {
        m_plot->xAxis->setRange(m_lonMin, m_lonMax);
        m_plot->yAxis->setRange(m_latMin, m_latMax);
    }
    updateColorBar();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}


// =============================================================================
// Helpers
// =============================================================================

/**
 * @brief Return the scalar value used for colour mapping for @p pt.
 *
 * ### OnFoot special case
 * In Altitude mode, OnFoot points have altitude = 0 (not recorded in flight).
 * Using the altitude would map all OnFoot points to the same deep-violet
 * colour.  Instead, we return pt.gravity, which is the meaningful scalar for
 * on-foot records and gives a useful colour distinction.
 */
double ScatterWidget::valueFor(const DataPoint& pt) const
{
    if (m_colorMode == 1)
        return pt.gravity;
    return (pt.mode == FlightMode::OnFoot) ? pt.gravity : pt.altitude;
}

/**
 * @brief Expand axis ranges to encompass @p pt if it falls outside.
 *
 * QCPAxis::setRange(expanded(value)) extends the range in the direction of
 * @p value without narrowing it.  We also force a rescale for the first two
 * points so the view initialises with a non-zero extent.
 */
void ScatterWidget::maybeExpandAxes(const DataPoint& pt)
{
    m_lonMin = qMin(m_lonMin, pt.lon);
    m_lonMax = qMax(m_lonMax, pt.lon);
    m_latMin = qMin(m_latMin, pt.lat);
    m_latMax = qMax(m_latMax, pt.lat);
    m_plot->xAxis->setRange(m_lonMin, m_lonMax);
    m_plot->yAxis->setRange(m_latMin, m_latMax);
}

/**
 * @brief Update the range label with the plottable's current min/max.
 *
 * The unit string is "g" in gravity mode, "m" in altitude mode.
 */
void ScatterWidget::updateColorBar()
{
    QString unit = (m_colorMode == 1) ? "g" : "m";
    m_rangeLabel->setText(
        QString("range: %1 – %2 %3")
            .arg(m_plottable->minVal(), 0, 'f', 1)
            .arg(m_plottable->maxVal(), 0, 'f', 1)
            .arg(unit)
    );
}


// =============================================================================
// Slots
// =============================================================================

/**
 * @brief Switch to a new colour mode and rebuild the scatter.
 */
void ScatterWidget::onColorModeChanged(int index)
{
    m_colorMode = index;
    rebuildAll(m_allPts);
}

/**
 * @brief Freeze or unfreeze the colour range.
 *
 * When unfreezing, auto-range is re-enabled by calling setAutoRange(true)
 * on the plottable and then rebuilding — this recomputes the range over all
 * current points.
 */
void ScatterWidget::onRangeToggled(bool fixed)
{
    m_fixedRange = fixed;
    m_minSpin->setEnabled(fixed);
    m_maxSpin->setEnabled(fixed);

    if (!fixed) {
        m_plottable->setAutoRange(true);
        rebuildAll(m_allPts);
    } else {
        // Seed spinboxes with the current auto-computed range, then freeze it.
        // Block signals so setting values doesn't trigger onRangeEdited yet.
        QSignalBlocker b1(m_minSpin), b2(m_maxSpin);
        m_minSpin->setValue(m_plottable->minVal());
        m_maxSpin->setValue(m_plottable->maxVal());
        m_plottable->setRange(m_plottable->minVal(), m_plottable->maxVal());
    }
    updateColorBar();
}

void ScatterWidget::onRangeEdited()
{
    if (!m_fixedRange) return;
    double lo = m_minSpin->value();
    double hi = m_maxSpin->value();
    if (lo >= hi) return;               // ignore invalid range
    m_plottable->setRange(lo, hi);
    m_plot->replot();                   // colours recomputed in draw() — no rebuild needed
    updateColorBar();
}

/**
 * @brief Show a hover tooltip for the nearest data point.
 *
 * ### Nearest-point search
 * For each point in m_allPts:
 *   1. Convert (lon, lat) to pixel coordinates using the current axis ranges.
 *   2. Compute the squared pixel distance d² = dx² + dy².
 *   3. If d² < bestDist², record this point as the new best candidate.
 *
 * Squared distances are compared to avoid a sqrt() per point.  The threshold
 * is 18² = 324 px².  If no point falls within the threshold the tooltip is
 * hidden.
 *
 * ### Tooltip content
 * | Field   | Source                          |
 * |---------|---------------------------------|
 * | Mode    | FlightMode enum → string        |
 * | Lat/Lon | pt.lat, pt.lon                  |
 * | Value   | altitude+raycast OR gravity     |
 * | Heading | pt.heading                      |
 * | Dist    | pt.distDeg (angular from base)  |
 */
void ScatterWidget::onMouseMove(QMouseEvent* event)
{
    if (!m_plot->axisRect()->rect().contains(event->pos())) {
        QToolTip::hideText();
        return;
    }

    constexpr double THRESH_PX = 18.0;
    double bestDist2 = THRESH_PX * THRESH_PX;
    bool   found     = false;
    DataPoint best;

    for (const DataPoint& pt : m_allPts) {
        double px = m_plot->xAxis->coordToPixel(pt.lon);
        double py = m_plot->yAxis->coordToPixel(pt.lat);
        double dx = px - event->pos().x();
        double dy = py - event->pos().y();
        double d2 = dx*dx + dy*dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best      = pt;
            found     = true;
        }
    }

    if (found) {
        QString modeStr;
        switch (best.mode) {
        case FlightMode::Ship:   modeStr = "Ship";    break;
        case FlightMode::SRV:    modeStr = "SRV";     break;
        case FlightMode::OnFoot: modeStr = "On foot"; break;
        default:                 modeStr = "?";
        }
        QString valLine = (best.mode == FlightMode::OnFoot)
            ? QString("Gravity: %1 g").arg(best.gravity, 0, 'f', 3)
            : QString("Alt: %1 m  rc:%2").arg(best.altitude, 0, 'f', 1)
                                         .arg(best.altRaycast ? "✓" : "✗");
        QString tip = QString("%1\nLat: %2\nLon: %3\n%4\nHdg: %5°\nDist: %6°\nAz:  %7°")
            .arg(modeStr)
            .arg(best.lat,        0, 'f', 5)
            .arg(best.lon,        0, 'f', 5)
            .arg(valLine)
            .arg(best.heading)
            .arg(best.distDeg,    0, 'f', 4)
            .arg(best.azimuthDeg, 0, 'f', 2);
        QToolTip::showText(event->globalPos(), tip, m_plot);
    } else {
        QToolTip::hideText();
    }
}
