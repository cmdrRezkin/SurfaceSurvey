/**
 * @file AzimuthWidget.cpp
 * @brief Implementation of AzimuthWidget — azimuth-slice profile plot.
 */
#include "AzimuthWidget.h"
#include "qcustomplot.h"
#include <QVBoxLayout>
#include <QToolTip>


// =============================================================================
// Constructor
// =============================================================================

/**
 * @brief Create the QCustomPlot and call setupPlot().
 */
AzimuthWidget::AzimuthWidget(QWidget* parent)
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
 * @brief Configure the QCustomPlot with the dark theme, legend, and title.
 *
 * Styling is identical to ProfileWidget.  The legend click signal is connected
 * to onLegendClick() for group toggling.  Mouse tracking is enabled so that
 * onMouseMove() fires without requiring a button press.
 */
void AzimuthWidget::setupPlot()
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

    m_title = new QCPTextElement(m_plot, QString(), QFont("sans", 9));
    m_title->setTextColor(Qt::white);
    m_plot->plotLayout()->insertRow(0);
    m_plot->plotLayout()->addElement(0, 0, m_title);

    connect(m_plot, &QCustomPlot::legendClick, this, &AzimuthWidget::onLegendClick);
    connect(m_plot, &QCustomPlot::mouseMove,   this, &AzimuthWidget::onMouseMove);
    m_plot->setMouseTracking(true);
}


// =============================================================================
// showBucket — full refresh
// =============================================================================

/**
 * @brief Redraw the azimuth-slice profile for a given @p bucket.
 *
 * Calls clearPlot() to reset, then addModeCurve() for each mode, then
 * auto-scales both axes and updates the title.
 *
 * @param bucket    Azimuth bucket in degrees (a multiple of the bin width).
 * @param shipPts   Ship-mode points with azimuthDeg in this bucket.
 * @param srvPts    SRV-mode points in this bucket.
 * @param footPts   OnFoot-mode points in this bucket.
 */
void AzimuthWidget::showBucket(int bucket,
                               const QVector<DataPoint>& shipPts,
                               const QVector<DataPoint>& srvPts,
                               const QVector<DataPoint>& footPts)
{
    clearPlot();

    addModeCurve(shipPts, QColor(0, 220, 255),  "Ship");
    addModeCurve(srvPts,  QColor(0, 255, 120),  "SRV");
    addModeCurve(footPts, QColor(255, 220, 80), "OnFoot");

    m_plot->xAxis->rescale(true);
    m_plot->yAxis->rescale(true);
    m_title->setText(QString("Azimuth-slice profile — azimuth %1°").arg(bucket));
    m_plot->replot();
}


// =============================================================================
// onLegendClick — group visibility toggle
// =============================================================================

/**
 * @brief Toggle a group's visibility when its legend entry is clicked.
 *
 * ### Sequence
 * 1. Resolve the clicked item to a QCPPlottableLegendItem.
 * 2. Check the plottable is a representative in m_groupGraphs.
 * 3. Flip the representative's visibility.
 * 4. Apply the same visibility to all member data graphs.
 * 5. Dim the legend text when the group is hidden.
 * 6. Replot.
 *
 * This is identical in logic to ProfileWidget::onLegendClick().
 */
void AzimuthWidget::onLegendClick(QCPLegend*, QCPAbstractLegendItem* item, QMouseEvent*)
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
// onMouseMove — hover tooltip
// =============================================================================

/**
 * @brief Show a tooltip for the nearest visible data point under the cursor.
 *
 * ### Search algorithm
 * For each data graph (key) in m_graphData, skipping invisible ones:
 *   - For each DataPoint, compute the screen position (distDeg → pixel X,
 *     altitude → pixel Y).
 *   - Compare squared pixel distance d² to the running minimum bestDist².
 *   - Keep the closest point within 18² = 324 px².
 *
 * ### Tooltip content
 * | Line     | Source                              |
 * |----------|-------------------------------------|
 * | Mode     | pt.mode → "Ship" / "SRV" / "On foot"|
 * | Azimuth  | pt.azimuthDeg (°)                   |
 * | Altitude | pt.altitude (m)                     |
 * | Lat/Lon  | pt.lat, pt.lon                      |
 * | Dist     | pt.distDeg (°)                      |
 * | Heading  | pt.heading (°)                      |
 */
void AzimuthWidget::onMouseMove(QMouseEvent* event)
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
        QString modeStr;
        switch (best.mode) {
        case FlightMode::Ship:   modeStr = "Ship";    break;
        case FlightMode::SRV:    modeStr = "SRV";     break;
        case FlightMode::OnFoot: modeStr = "On foot"; break;
        default:                 modeStr = "?";
        }
        QString tip = QString("%1\nAzimuth: %2°\nAlt:     %3 m\nLat:     %4\nLon:     %5\nDist:    %6°\nHdg:     %7°")
            .arg(modeStr)
            .arg(best.azimuthDeg, 0, 'f', 2)
            .arg(best.altitude,   0, 'f', 1)
            .arg(best.lat,        0, 'f', 5)
            .arg(best.lon,        0, 'f', 5)
            .arg(best.distDeg,    0, 'f', 4)
            .arg(best.heading);
        QToolTip::showText(event->globalPos(), tip, m_plot);
    } else {
        QToolTip::hideText();
    }
}


// =============================================================================
// Helpers
// =============================================================================

/**
 * @brief Reset the plot and create three group representative graphs.
 *
 * Removes all QCPGraph objects and clears the group/data maps.  Creates three
 * representative graphs ("Ship", "SRV", "OnFoot") with empty member lists.
 * Note: this widget has no raycast markers, so no item cleanup is needed.
 */
void AzimuthWidget::clearPlot()
{
    m_plot->clearGraphs();
    m_groupGraphs.clear();
    m_graphData.clear();

    auto addGroup = [&](const QString& name, QColor col) -> QCPGraph* {
        QCPGraph* rep = m_plot->addGraph();
        rep->setName(name);
        rep->setPen(QPen(col, 1.5, Qt::SolidLine));
        rep->setLineStyle(QCPGraph::lsLine);
        m_groupGraphs[rep] = {};
        return rep;
    };
    addGroup("Ship",   QColor(0, 220, 255));
    addGroup("SRV",    QColor(0, 255, 120));
    addGroup("OnFoot", QColor(255, 220, 80));
}

/**
 * @brief Plot a set of same-mode points as a scatter disc graph.
 *
 * ### Data coordinates
 * X = pt.distDeg  — angular distance from base, NOT the azimuth angle.
 * Y = pt.altitude — altitude in metres.
 *
 * The azimuth was already used to select which points appear in the list;
 * the profile view then shows how altitude varies with distance along that
 * azimuthal direction.
 *
 * @param pts       Points to plot.
 * @param col       Disc colour.
 * @param groupName Name of the representative group to register with.
 */
void AzimuthWidget::addModeCurve(const QVector<DataPoint>& pts,
                                 const QColor& col, const QString& groupName)
{
    if (pts.isEmpty()) return;

    // Find the representative for this group
    QCPGraph* rep = nullptr;
    for (auto it = m_groupGraphs.begin(); it != m_groupGraphs.end(); ++it) {
        if (it.key()->name() == groupName) { rep = it.key(); break; }
    }
    if (!rep) return;

    QCPGraph* g = m_plot->addGraph();
    g->setName(QString());          // no legend entry for data graphs
    g->setLineStyle(QCPGraph::lsNone);
    QCPScatterStyle ss;
    ss.setShape(QCPScatterStyle::ssDisc);
    ss.setSize(4);
    ss.setPen(QPen(col));
    ss.setBrush(QBrush(col));
    g->setScatterStyle(ss);

    for (const DataPoint& pt : pts)
        g->addData(pt.distDeg, pt.altitude);   // X = distance, NOT azimuth

    m_graphData[g] = pts;
    m_groupGraphs[rep].append(g);
}
