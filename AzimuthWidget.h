/**
 * @file AzimuthWidget.h
 * @brief Altitude-vs-angular-distance profile sliced by geometric azimuth.
 *
 * AzimuthWidget shows the same axes as ProfileWidget (altitude on Y, angular
 * distance from base on X) but selects data by the geometric bearing from the
 * base (DataPoint::azimuthDeg) rather than by the ship's compass heading.
 */
#pragma once
#include "DataPoint.h"
#include <QWidget>
#include <QVector>
#include <QMap>
#include <QList>

class QCustomPlot;
class QCPGraph;
class QCPTextElement;
class QCPLegend;
class QCPAbstractLegendItem;
class QMouseEvent;


/**
 * @brief Altitude (y) vs. angular distance from base (x) sliced by azimuth bucket.
 *
 * ### How it differs from ProfileWidget
 * ProfileWidget groups data by the ship's **compass heading** at the time of
 * recording — useful for seeing altitude profiles along a specific direction of
 * travel.
 *
 * AzimuthWidget groups data by the **geometric bearing** from the survey base
 * to the measurement position (DataPoint::azimuthDeg, computed by
 * SurveyData::azimuthDeg()).  This answers the question: "what does the terrain
 * look like in the direction X° from the base?", regardless of which way the
 * ship was pointing.
 *
 * ### Data shown
 * showBucket() receives three lists of points (Ship, SRV, OnFoot) that all
 * fall in the selected azimuth bucket.  They are plotted as disc scatter:
 *   - **Ship** — cyan
 *   - **SRV** — green
 *   - **OnFoot** — yellow
 *
 * ### Group-based legend toggle
 * The same representative/member pattern as ProfileWidget is used.  Three
 * representative graphs ("Ship", "SRV", "OnFoot") are created by clearPlot()
 * and tracked in m_groupGraphs.  Clicking a legend item toggles all member
 * graphs of that group.
 *
 * ### Hover tooltip
 * onMouseMove() performs a pixel-space nearest-neighbour search (threshold
 * 18 px) and shows a QToolTip including mode, azimuth, altitude, lat/lon,
 * angular distance, and compass heading.
 *
 * @see ProfileWidget — for the heading-based counterpart.
 * @see SurveyData::pointsByAzimuth() — for the data source.
 * @see MainWindow::refreshProfile() — for how showBucket() is called.
 */
class AzimuthWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Construct and set up the QCustomPlot.
     * @param parent  Owning widget.
     */
    explicit AzimuthWidget(QWidget* parent = nullptr);

public slots:
    /**
     * @brief Redraw the azimuth-slice profile for @p bucket.
     *
     * Clears the current plot, then calls addModeCurve() for each of the
     * three point lists.  Axes are auto-scaled and the title is updated.
     *
     * @param bucket    The azimuth bucket value (in degrees, a multiple of
     *                  the bin width).
     * @param shipPts   Ship-mode points whose azimuthDeg falls in this bucket.
     * @param srvPts    SRV-mode points in this bucket.
     * @param footPts   OnFoot-mode points in this bucket.
     */
    void showBucket(int bucket,
                    const QVector<DataPoint>& shipPts,
                    const QVector<DataPoint>& srvPts,
                    const QVector<DataPoint>& footPts);

private slots:
    /**
     * @brief Toggle visibility of all graphs in a legend group.
     *
     * Same algorithm as ProfileWidget::onLegendClick() — see that function
     * for the detailed description of the representative/member pattern.
     *
     * @param legend  The QCPLegend clicked (unused).
     * @param item    The legend item clicked.
     * @param event   The mouse event (unused).
     */
    void onLegendClick(QCPLegend* legend, QCPAbstractLegendItem* item, QMouseEvent* event);

    /**
     * @brief Show a hover tooltip for the nearest visible data point.
     *
     * Searches m_graphData using the same O(N) pixel-space scan as
     * ProfileWidget::onMouseMove().  The tooltip includes the azimuth bucket,
     * altitude, lat/lon, angular distance from base, and compass heading.
     *
     * @param event  Mouse move event with cursor position.
     */
    void onMouseMove(QMouseEvent* event);

private:
    /**
     * @brief Apply the dark theme, configure axes, legend, and title.
     *
     * Identical styling to ProfileWidget (same X and Y axis labels, same dark
     * colour scheme).  The title text element is initialised empty and updated
     * by showBucket().
     */
    void setupPlot();

    /**
     * @brief Reset the plot and create the three group representative graphs.
     *
     * Removes all QCPGraph objects via clearGraphs(), clears m_groupGraphs and
     * m_graphData, then creates three representative graphs:
     *   - "Ship"   (cyan,   solid, 1.5 px)
     *   - "SRV"    (green,  solid, 1.5 px)
     *   - "OnFoot" (yellow, solid, 1.5 px)
     *
     * Note: unlike ProfileWidget, this widget does not distinguish Forward from
     * Backward for Ship — it simply shows all Ship points for the azimuth bucket
     * as one group.
     */
    void clearPlot();

    /**
     * @brief Plot a set of same-mode points as a disc scatter graph.
     *
     * Creates a data graph with no line (lsNone), disc markers, registers it
     * under the named group in m_groupGraphs, and stores the DataPoints in
     * m_graphData for hover lookup.
     *
     * All points are plotted at (pt.distDeg, pt.altitude) regardless of mode —
     * the X axis is always the angular distance from base, not the azimuth.
     *
     * @param pts       Points to display.
     * @param col       Disc colour.
     * @param groupName Name of the representative graph ("Ship", "SRV", "OnFoot").
     */
    void addModeCurve(const QVector<DataPoint>& pts,
                      const QColor& col, const QString& groupName);

    // ── QCustomPlot objects ───────────────────────────────────────────────────
    QCustomPlot*    m_plot  = nullptr;  ///< The QCustomPlot canvas.
    QCPTextElement* m_title = nullptr;  ///< Title element above the axis rect.

    // ── Group / data tracking ─────────────────────────────────────────────────

    /**
     * @brief Maps each representative graph → list of its data graphs.
     *
     * Used by onLegendClick() to toggle an entire group with one click, and
     * by clearPlot() to reset the groups.
     */
    QMap<QCPGraph*, QList<QCPGraph*>>  m_groupGraphs;

    /**
     * @brief Maps each data graph → vector of DataPoints it displays.
     *
     * Used by onMouseMove() for the hover nearest-neighbour search.
     * Keys in this map are always data graphs (not representative graphs).
     */
    QMap<QCPGraph*, QVector<DataPoint>> m_graphData;
};
