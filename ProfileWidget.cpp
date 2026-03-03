/**
 * @file ProfileWidget.cpp
 * @brief Implementation of ProfileWidget — altitude-vs-distance profile plot.
 */
#include "ProfileWidget.h"
#include "qcustomplot.h"
#include <QVBoxLayout>
#include <QToolTip>


// =============================================================================
// Constructor
// =============================================================================

/**
 * @brief Create the QCustomPlot and call setupPlot().
 */
ProfileWidget::ProfileWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_plot = new QCustomPlot(this);
    lay->addWidget(m_plot);

    setupPlot();
}


// =============================================================================
// setupPlot
// =============================================================================

/**
 * @brief Configure the QCustomPlot appearance, legend, interactions, and title.
 *
 * ### Title row
 * A QCPTextElement is inserted into row 0 of the plot layout (above the axis
 * rect).  Its text is updated by showHeading() on each refresh.
 *
 * ### Legend click signal
 * QCustomPlot::legendClick is connected here so that onLegendClick() fires
 * whenever the user clicks any item in the legend.
 */
void ProfileWidget::setupPlot()
{
    m_plot->setBackground(QBrush(QColor(8, 8, 24)));
    m_plot->axisRect()->setBackground(QBrush(QColor(10, 10, 20)));

    m_plot->xAxis->setLabel("Angular dist. from base (°)");
    m_plot->yAxis->setLabel("Altitude (m)");

    auto styleAxis = [](QCPAxis* ax) {
        ax->setLabelColor(Qt::white);
        ax->setTickLabelColor(Qt::white);
        ax->setBasePen(QPen(QColor(80, 80, 80)));
        ax->setTickPen(QPen(QColor(80, 80, 80)));
        ax->setSubTickPen(QPen(QColor(50, 50, 50)));
        ax->grid()->setPen(QPen(QColor(40, 40, 60), 1, Qt::DotLine));
    };
    styleAxis(m_plot->xAxis);
    styleAxis(m_plot->yAxis);

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(15, 15, 35, 200)));
    m_plot->legend->setTextColor(Qt::white);
    m_plot->legend->setBorderPen(QPen(QColor(80, 80, 80)));
    m_plot->legend->setSelectableParts(QCPLegend::spItems);

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // Title element — text set later by showHeading()
    m_title = new QCPTextElement(m_plot, QString(), QFont("sans", 9));
    m_title->setTextColor(Qt::white);
    m_plot->plotLayout()->insertRow(0);
    m_plot->plotLayout()->addElement(0, 0, m_title);

    connect(m_plot, &QCustomPlot::legendClick,
            this, &ProfileWidget::onLegendClick);
    connect(m_plot, &QCustomPlot::mouseMove,
            this, &ProfileWidget::onMouseMove);
    m_plot->setMouseTracking(true);
}


// =============================================================================
// showHeading — full refresh
// =============================================================================

/**
 * @brief Redraw the entire profile for @p headingBucket.
 *
 * ### Opacity assignment for multiple legs
 * When there are N legs in one direction, their opacities are evenly spaced
 * from 0.25 (oldest) to 1.0 (newest) using the formula:
 * @code
 *   opacity[i] = 0.25 + 0.75 * (i + 1) / N
 * @endcode
 * So for N = 3:  i=0 → 0.50,  i=1 → 0.75,  i=2 → 1.00.
 * For N = 1:     i=0 → 1.00 (single leg is always fully opaque).
 *
 * The legs vector from SurveyData preserves arrival order (index 0 is the
 * oldest leg), so the most recent leg is always drawn fully opaque.
 */
void ProfileWidget::showHeading(int headingBucket,
                                const QVector<Leg>& legs,
                                const QVector<DataPoint>& srvPts,
                                const QVector<DataPoint>& footPts)
{
    m_currentHeading = headingBucket;
    clearPlot();

    // ── Ship legs ──────────────────────────────────────────────────────────
    if (!legs.isEmpty()) {
        // Separate into forward and backward subsets
        QVector<const Leg*> fwdLegs, bckLegs;
        for (const auto& leg : legs) {
            if (leg.isForward())  fwdLegs.append(&leg);
            else                   bckLegs.append(&leg);
        }

        // Draw each group with opacity increasing toward the most recent leg
        auto drawGroup = [&](const QVector<const Leg*>& group) {
            int n = group.size();
            for (int i = 0; i < n; ++i) {
                double opacity = 0.25 + 0.75 * (i + 1.0) / n;
                addLegCurve(*group[i], opacity);
                addRaycastMarkers(*group[i]);
            }
        };

        drawGroup(fwdLegs);
        drawGroup(bckLegs);
    }

    // ── SRV and OnFoot raw points ──────────────────────────────────────────
    addModeCurve(srvPts,  QColor(0, 255, 120), "SRV");
    addModeCurve(footPts, QColor(255, 220, 80), "OnFoot");

    m_plot->xAxis->rescale(true);
    m_plot->yAxis->rescale(true);
    m_title->setText(QString("Heading %1°  |  ─ Ship Fwd  ╌ Ship Bck  │ Raycast switch")
                         .arg(headingBucket));
    m_plot->replot();
}


// =============================================================================
// updateActiveLeg — live update
// =============================================================================

/**
 * @brief Update the live forward or backward graph with the latest active-leg data.
 *
 * ### Why rebuild from scratch rather than appending?
 * QCPGraph stores data in a sorted container (QCPDataContainer).  Appending
 * out-of-order keys causes undefined rendering behaviour.  Rebuilding from
 * scratch guarantees correctness and is fast because active legs are short
 * (typically < 100 points at 2 Hz for a 1-minute leg).
 *
 * ### Why a separate live graph?
 * The completed legs drawn by showHeading() should not be modified when a new
 * point arrives.  Using a dedicated graph lets the live update touch only those
 * few pixels without triggering a full redraw of all leg curves.
 *
 * ### Live graph appearance
 * Forward live leg: bright cyan (0, 220, 255), 2 px, disc size 4.
 * Backward live leg: orange (255, 160, 0), 2 px, dashed, disc size 4.
 */
void ProfileWidget::updateActiveLeg(const Leg& leg)
{
    if (leg.headingBucket != m_currentHeading) return;
    if (leg.points.isEmpty()) return;

    // Select or create the correct live graph
    QCPGraph*& g = leg.isForward() ? m_activeFwdGraph : m_activeBckGraph;

    if (!g) {
        g = m_plot->addGraph();
        QPen pen;
        if (leg.isForward()) {
            pen = QPen(QColor(0, 220, 255), 2.0);
            g->setName("Ship Fwd (live)");
        } else {
            pen = QPen(QColor(255, 160, 0), 2.0, Qt::DashLine);
            g->setName("Ship Bck (live)");
        }
        g->setLineStyle(QCPGraph::lsNone);
        QCPScatterStyle ss;
        ss.setShape(QCPScatterStyle::ssDisc);
        ss.setSize(4);
        ss.setPen(pen);
        ss.setBrush(QBrush(pen.color()));
        g->setScatterStyle(ss);
    }

    // Rebuild data from scratch
    g->data()->clear();
    for (const auto& pt : leg.points) {
        if (pt.altRaycast)
            g->addData(pt.distDeg, pt.altitude);
    }

    m_plot->yAxis->rescale(true);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}


// =============================================================================
// onLegendClick — group visibility toggle
// =============================================================================

/**
 * @brief Toggle the visibility of a complete group when its legend entry is clicked.
 *
 * ### Pattern: representative graph → member graphs
 * Only representative graphs appear in the legend (they have non-empty names).
 * When the user clicks a legend entry:
 *   1. We cast the item to QCPPlottableLegendItem to get the plottable.
 *   2. We check that the plottable is a key in m_groupGraphs (i.e. it is a
 *      representative, not a data graph).
 *   3. We flip the representative's visibility.
 *   4. We apply the same visibility to all member graphs.
 *   5. We dim the legend text to QColor(90, 90, 90) when hidden.
 */
void ProfileWidget::onLegendClick(QCPLegend*, QCPAbstractLegendItem* item, QMouseEvent*)
{
    auto* pli = qobject_cast<QCPPlottableLegendItem*>(item);
    if (!pli) return;
    auto* rep = qobject_cast<QCPGraph*>(pli->plottable());
    if (!rep || !m_groupGraphs.contains(rep)) return;

    bool nowVisible = !rep->visible();
    rep->setVisible(nowVisible);
    for (QCPGraph* g : m_groupGraphs[rep])
        g->setVisible(nowVisible);

    item->setTextColor(nowVisible ? Qt::white : QColor(90, 90, 90));

    m_plot->replot();
}


// =============================================================================
// Helpers
// =============================================================================

/**
 * @brief Reset the plot to its empty baseline state with four group stubs.
 *
 * All QCPGraph objects are removed by clearGraphs().  The raycast marker items
 * (QCPItemLine and QCPItemText) must be removed individually because
 * clearGraphs() does not touch items.  The m_markers vector tracks them.
 *
 * After cleanup, four representative graphs are created (no data, just names
 * and colours/styles) and stored in m_groupGraphs with empty member lists.
 */
void ProfileWidget::clearPlot()
{
    m_plot->clearGraphs();
    for (auto* item : m_markers)
        m_plot->removeItem(item);
    m_markers.clear();
    m_activeFwdGraph = nullptr;
    m_activeBckGraph = nullptr;
    m_groupGraphs.clear();
    m_graphData.clear();

    // Create representative (legend-entry) graphs — no data
    auto addGroup = [&](const QString& name, QColor col, Qt::PenStyle style) -> QCPGraph* {
        QCPGraph* rep = m_plot->addGraph();
        rep->setName(name);
        rep->setPen(QPen(col, 1.5, style));
        rep->setLineStyle(QCPGraph::lsLine);
        m_groupGraphs[rep] = {};
        return rep;
    };
    addGroup("Ship Fwd", QColor(0, 220, 255),  Qt::SolidLine);
    addGroup("Ship Bck", QColor(255, 160, 0),  Qt::DashLine);
    addGroup("SRV",      QColor(0, 255, 120),  Qt::SolidLine);
    addGroup("OnFoot",   QColor(255, 220, 80), Qt::DotLine);
}

/**
 * @brief Draw one ship leg as a scatter-only disc graph.
 *
 * ### Point filtering
 * Two filters are applied before a point is added to the graph:
 *   1. **altRaycast == false**: skipped — radius-mode altitude is unreliable.
 *   2. **altitude ≥ 1200 m**: skipped — spike filter for spurious spikes.
 *
 * ### Group registration
 * The new graph is appended to the member list of the "Ship Fwd" or "Ship Bck"
 * representative graph in m_groupGraphs.  This is what makes the legend toggle
 * work: hiding the representative hides all its members.
 */
void ProfileWidget::addLegCurve(const Leg& leg, double opacity)
{
    bool isForward = leg.isForward();
    QColor base = isForward ? QColor(0, 220, 255) : QColor(255, 160, 0);
    base.setAlphaF(opacity);

    QCPGraph* g = m_plot->addGraph();
    g->setLineStyle(QCPGraph::lsNone);
    g->setName(QString());   // no legend entry for data graphs
    QCPScatterStyle ss;
    ss.setShape(QCPScatterStyle::ssDisc);
    ss.setSize(4);
    ss.setPen(QPen(base));
    ss.setBrush(QBrush(base));
    g->setScatterStyle(ss);

    // Filter and add data points
    QVector<DataPoint> plotted;
    for (const auto& pt : leg.points) {
        if (pt.altRaycast && pt.altitude < 1200) {
            g->addData(pt.distDeg, pt.altitude);
            plotted.append(pt);
        }
    }
    m_graphData[g] = std::move(plotted);

    // Register with the "Ship Fwd" or "Ship Bck" group
    const QString groupName = isForward ? "Ship Fwd" : "Ship Bck";
    for (auto it = m_groupGraphs.begin(); it != m_groupGraphs.end(); ++it) {
        if (it.key()->name() == groupName) {
            it.value().append(g);
            break;
        }
    }
}

/**
 * @brief Draw SRV or OnFoot raw points in a named colour group.
 *
 * No filtering is applied (SRV/OnFoot altitudes are kept as-is; OnFoot
 * altitude is always 0 but the spatial coverage along the X axis is still
 * useful).
 */
void ProfileWidget::addModeCurve(const QVector<DataPoint>& pts,
                                 const QColor& col, const QString& groupName)
{
    if (pts.isEmpty()) return;

    // Find the representative graph for this group
    QCPGraph* rep = nullptr;
    for (auto it = m_groupGraphs.begin(); it != m_groupGraphs.end(); ++it) {
        if (it.key()->name() == groupName) { rep = it.key(); break; }
    }
    if (!rep) return;

    QCPGraph* g = m_plot->addGraph();
    g->setName(QString());
    g->setLineStyle(QCPGraph::lsNone);
    QCPScatterStyle ss;
    ss.setShape(QCPScatterStyle::ssDisc);
    ss.setSize(3);
    ss.setPen(QPen(col));
    ss.setBrush(QBrush(col));
    g->setScatterStyle(ss);

    for (const auto& pt : pts)
        g->addData(pt.distDeg, pt.altitude);

    m_graphData[g] = pts;
    m_groupGraphs[rep].append(g);
}

/**
 * @brief Scan leg.points for altRaycast transitions and draw vertical markers.
 *
 * ### Transition detection
 * We walk the points array once, maintaining the altRaycast flag of the
 * previous point (prevRaycast).  When pts[i].altRaycast != prevRaycast, a
 * transition has occurred at pts[i].distDeg.
 *
 * ### Markers drawn at each transition
 * - **QCPItemLine**: a vertical line from y = −1e6 to y = +1e6, clipped to the
 *   axis rect.  This appears as a full-height vertical rule.
 * - **QCPItemText**: a small label ("ray→rad" or "rad→ray") anchored at the
 *   bottom-left of the transition x coordinate.
 *
 * Both items are appended to m_markers so they are properly cleaned up in
 * clearPlot().  Without this, stale items from the previous heading would
 * remain on screen.
 *
 * @param leg  The ship leg to scan.
 */
void ProfileWidget::addRaycastMarkers(const Leg& leg)
{
    const QVector<DataPoint>& pts = leg.points;
    if (pts.size() < 2) return;

    const QPen markerPen(QColor(220, 0, 220), 1.0, Qt::DotLine);
    bool prevRaycast = pts.first().altRaycast;

    for (int i = 1; i < pts.size(); ++i) {
        bool curr = pts[i].altRaycast;
        if (curr != prevRaycast) {
            double x = pts[i].distDeg;

            // Vertical dotted line spanning the full plot height
            QCPItemLine* line = new QCPItemLine(m_plot);
            line->start->setCoords(x, -1e6);
            line->end->setCoords(x,  1e6);
            line->setClipToAxisRect(true);
            line->setPen(markerPen);
            m_markers.append(line);

            // Transition label
            QCPItemText* lbl = new QCPItemText(m_plot);
            lbl->position->setType(QCPItemPosition::ptPlotCoords);
            lbl->position->setCoords(x, 0);
            lbl->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
            lbl->setText(curr ? "ray→rad" : "rad→ray");
            lbl->setColor(QColor(220, 0, 220));
            lbl->setFont(QFont("monospace", 6));
            m_markers.append(lbl);
        }
        prevRaycast = curr;
    }
}


// =============================================================================
// onMouseMove — hover tooltip
// =============================================================================

/**
 * @brief Find the nearest visible data point and show a tooltip.
 *
 * ### Search scope
 * Only graphs present in m_graphData are searched (representative graphs
 * have no DataPoints and are not in this map).  Invisible graphs are skipped
 * (they belong to a toggled-off group).
 *
 * ### Squared-distance optimisation
 * The threshold 18² = 324 px² is compared to d² directly, avoiding a
 * sqrt() per point.  Only the final result needs a sqrt to show the actual
 * pixel distance, which we don't need — so no sqrt() is ever called.
 *
 * ### Tooltip content
 * | Line     | Field                         |
 * |----------|-------------------------------|
 * | Dist     | pt.distDeg (°)                |
 * | Alt      | pt.altitude (m)               |
 * | Lat/Lon  | pt.lat, pt.lon                |
 * | Heading  | pt.heading (°)                |
 */
void ProfileWidget::onMouseMove(QMouseEvent* event)
{
    if (!m_plot->axisRect()->rect().contains(event->pos())) {
        QToolTip::hideText();
        return;
    }

    constexpr double THRESH_PX = 18.0;
    double bestDist2 = THRESH_PX * THRESH_PX;
    bool   found     = false;
    DataPoint best;

    for (auto it = m_graphData.cbegin(); it != m_graphData.cend(); ++it) {
        if (!it.key()->visible()) continue;
        for (const DataPoint& pt : it.value()) {
            double px = m_plot->xAxis->coordToPixel(pt.distDeg);
            double py = m_plot->yAxis->coordToPixel(pt.altitude);
            double dx = px - event->pos().x();
            double dy = py - event->pos().y();
            double d2 = dx*dx + dy*dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                best      = pt;
                found     = true;
            }
        }
    }

    if (found) {
        QString tip = QString("Dist: %1°\nAlt:  %2 m\nLat:  %3\nLon:  %4\nHdg:  %5°\nAz:   %6°")
            .arg(best.distDeg,    0, 'f', 4)
            .arg(best.altitude,   0, 'f', 1)
            .arg(best.lat,        0, 'f', 5)
            .arg(best.lon,        0, 'f', 5)
            .arg(best.heading)
            .arg(best.azimuthDeg, 0, 'f', 2);
        QToolTip::showText(event->globalPos(), tip, m_plot);
    } else {
        QToolTip::hideText();
    }
}
