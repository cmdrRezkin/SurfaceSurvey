/**
 * @file RaycastBoundaryWidget.cpp
 * @brief Implementation of RaycastBoundaryWidget.
 */
#include "RaycastBoundaryWidget.h"
#include "qcustomplot.h"
#include <QVBoxLayout>
#include <QToolTip>


// =============================================================================
// Constructor
// =============================================================================

RaycastBoundaryWidget::RaycastBoundaryWidget(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Raycast Boundary Map");
    setMinimumSize(500, 500);
    resize(600, 620);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_plot = new QCustomPlot(this);
    lay->addWidget(m_plot);

    setupPlot();
}


// =============================================================================
// setupPlot
// =============================================================================

void RaycastBoundaryWidget::setupPlot()
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

    // Title
    auto* title = new QCPTextElement(
        m_plot, "Raycast boundary  (altRaycast: true → false)", QFont("sans", 9));
    title->setTextColor(Qt::white);
    m_plot->plotLayout()->insertRow(0);
    m_plot->plotLayout()->addElement(0, 0, title);

    // Legend
    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(15, 15, 35, 200)));
    m_plot->legend->setTextColor(Qt::white);
    m_plot->legend->setBorderPen(QPen(QColor(80, 80, 80)));

    // ── Transition scatter graph ──────────────────────────────────────────────
    m_transGraph = m_plot->addGraph();
    m_transGraph->setName("ray→0 boundary");
    m_transGraph->setLineStyle(QCPGraph::lsNone);
    {
        QCPScatterStyle ss;
        ss.setShape(QCPScatterStyle::ssTriangle);
        ss.setSize(9);
        ss.setPen(QPen(QColor(200, 80, 30)));
        ss.setBrush(QBrush(QColor(255, 120, 50)));
        m_transGraph->setScatterStyle(ss);
    }

    // ── Centroid graph (single point, yellow cross) ───────────────────────────
    m_centroidGraph = m_plot->addGraph();
    m_centroidGraph->setName("Centroid");
    m_centroidGraph->setLineStyle(QCPGraph::lsNone);
    {
        QCPScatterStyle cs;
        cs.setShape(QCPScatterStyle::ssCross);
        cs.setSize(16);
        cs.setPen(QPen(QColor(255, 230, 0), 2.5));
        m_centroidGraph->setScatterStyle(cs);
    }

    // ── Base marker ───────────────────────────────────────────────────────────
    m_baseMarker = new QCPItemText(m_plot);
    m_baseMarker->setText("★ BASE");
    m_baseMarker->setColor(Qt::white);
    m_baseMarker->setFont(QFont("sans-serif", 9, QFont::Bold));
    m_baseMarker->position->setType(QCPItemPosition::ptPlotCoords);
    m_baseMarker->position->setCoords(0, 0);
    m_baseMarker->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);

    // ── Mouse hover ───────────────────────────────────────────────────────────
    connect(m_plot, &QCustomPlot::mouseMove, this, &RaycastBoundaryWidget::onMouseMove);
    m_plot->setMouseTracking(true);
}


// =============================================================================
// Public interface
// =============================================================================

void RaycastBoundaryWidget::setBase(double lat, double lon)
{
    m_baseMarker->position->setCoords(lon, lat);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void RaycastBoundaryWidget::onNewPoint(const DataPoint& pt)
{
    int modeKey = static_cast<int>(pt.mode);

    // Default assumption for a mode seen for the first time: was in raycast
    bool prev = m_prevRaycast.value(modeKey, true);
    m_prevRaycast[modeKey] = pt.altRaycast;

    // Only act on the true → false transition
    if (!prev || pt.altRaycast)
        return;

    // Record the transition position
    m_transGraph->addData(pt.lon, pt.lat);
    m_transPoints.append(pt);
    m_sumLon += pt.lon;
    m_sumLat += pt.lat;
    ++m_count;

    updateCentroid();

    // Update axis ranges to cover all transition points
    m_lonMin = qMin(m_lonMin, pt.lon);  m_lonMax = qMax(m_lonMax, pt.lon);
    m_latMin = qMin(m_latMin, pt.lat);  m_latMax = qMax(m_latMax, pt.lat);
    m_plot->xAxis->setRange(m_lonMin, m_lonMax);
    m_plot->yAxis->setRange(m_latMin, m_latMax);

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}


// =============================================================================
// Helpers
// =============================================================================

void RaycastBoundaryWidget::updateCentroid()
{
    m_centroidLon = m_sumLon / m_count;
    m_centroidLat = m_sumLat / m_count;

    m_centroidGraph->data()->clear();
    m_centroidGraph->addData(m_centroidLon, m_centroidLat);
}


// =============================================================================
// onMouseMove — hover tooltip
// =============================================================================

void RaycastBoundaryWidget::onMouseMove(QMouseEvent* event)
{
    if (!m_plot->axisRect()->rect().contains(event->pos())) {
        QToolTip::hideText();
        return;
    }

    constexpr double THRESH_PX = 18.0;
    const double thresh2 = THRESH_PX * THRESH_PX;

    // ── Centroid takes priority (larger marker) ───────────────────────────────
    if (m_count > 0) {
        double cx = m_plot->xAxis->coordToPixel(m_centroidLon);
        double cy = m_plot->yAxis->coordToPixel(m_centroidLat);
        double dx = cx - event->pos().x();
        double dy = cy - event->pos().y();
        if (dx*dx + dy*dy < thresh2) {
            QToolTip::showText(event->globalPos(),
                QString("Centroid  (n=%1)\nlat: %2°\nlon: %3°")
                    .arg(m_count)
                    .arg(m_centroidLat, 0, 'f', 5)
                    .arg(m_centroidLon, 0, 'f', 5),
                m_plot);
            return;
        }
    }

    // ── Nearest transition point ──────────────────────────────────────────────
    double bestDist2 = thresh2;
    bool   found     = false;
    DataPoint best;

    for (const DataPoint& pt : m_transPoints) {
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
        QToolTip::showText(event->globalPos(),
            QString("%1\nlat: %2°\nlon: %3°\nalt: %4 m")
                .arg(best.timestamp.toString("hh:mm:ss"))
                .arg(best.lat,      0, 'f', 5)
                .arg(best.lon,      0, 'f', 5)
                .arg(best.altitude, 0, 'f', 1),
            m_plot);
    } else {
        QToolTip::hideText();
    }
}
