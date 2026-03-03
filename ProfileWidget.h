/**
 * @file ProfileWidget.h
 * @brief Altitude-vs-angular-distance profile plot for a single heading bucket.
 *
 * ProfileWidget renders the "radial profile" for the heading bucket currently
 * selected in the MainWindow heading list.  The X axis is the angular distance
 * from the survey base (DataPoint::distDeg, in degrees of arc), and the Y axis
 * is altitude in metres.
 */
#pragma once
#include "DataPoint.h"
#include <QWidget>
#include <QVector>
#include <QMap>
#include <QList>

class QCustomPlot;
class QCPGraph;
class QCPItemLine;
class QCPAbstractItem;
class QCPTextElement;
class QCPLegend;
class QCPAbstractLegendItem;
class QMouseEvent;


/**
 * @brief Altitude (y) vs. angular distance from base (x) for one heading bucket.
 *
 * ### Data shown
 * When showHeading() is called with a bucket and three point-lists, this widget
 * displays:
 *   - **Ship Forward** legs — cyan dots (solid group)
 *   - **Ship Backward** legs — orange dots (dashed group)
 *   - **SRV** points — green dots
 *   - **OnFoot** points — yellow dots
 *   - **Raycast/radius transitions** — vertical magenta dotted lines where
 *     the altitude measurement method changes (altRaycast flips).
 *
 * ### Group-based legend toggle
 * The legend shows one entry per group ("Ship Fwd", "Ship Bck", "SRV",
 * "OnFoot").  Clicking a legend entry hides or shows all data graphs that
 * belong to that group, and dims/brightens the legend text accordingly.
 *
 * ### Graph organisation
 * The widget uses a **representative graph + member graph** pattern:
 *
 *   - Four *representative* graphs are created in clearPlot() with meaningful
 *     names ("Ship Fwd", etc.).  They carry no data; their only purpose is to
 *     appear in the legend and act as handles for the group toggle.
 *
 *   - Each data graph (one per leg, or one per mode curve) carries the actual
 *     data points and has an empty name so it does not appear in the legend.
 *
 *   - m_groupGraphs maps each representative → list of data graphs in the group.
 *     onLegendClick() uses this map to toggle all members when the legend entry
 *     is clicked.
 *
 * ### Opacity coding for multiple legs
 * When a heading bucket has more than one leg in the same direction (e.g. the
 * ship flew the same route three times), older legs are drawn more transparently
 * and the most recent leg is fully opaque.  This gives a visual history.
 *
 * ### Live update
 * While the ship is flying the currently selected heading, updateActiveLeg()
 * is called on every new point.  It maintains a separate live graph
 * (m_activeFwdGraph or m_activeBckGraph) that is rebuilt from scratch on
 * each call (fast because active legs are small) and triggers a queued replot.
 *
 * ### Hover tooltip
 * onMouseMove() performs an O(N) pixel-space nearest-neighbour search over
 * all visible data points and shows a QToolTip with the point's details.
 *
 * @see MainWindow::refreshProfile() — for how the slot is called.
 * @see SurveyData::legsForHeading() — for the leg data source.
 * @see SurveyData::pointsForHeading() — for the SRV/OnFoot data source.
 */
class ProfileWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Construct and set up the QCustomPlot.
     * @param parent  Owning widget.
     */
    explicit ProfileWidget(QWidget* parent = nullptr);

public slots:
    /**
     * @brief Full refresh — redraw everything for a given heading bucket.
     *
     * Called by MainWindow::refreshProfile() when:
     *   - The user clicks a heading in the list (onHeadingSelected).
     *   - A leg for the selected heading is finalised (onLegFinalized).
     *
     * ### What happens
     * 1. clearPlot() — removes all graphs, markers, and the active-leg pointers.
     * 2. Ship legs are sorted into Forward and Backward groups.
     *    Older legs get lower opacity (0.25 minimum), the newest is fully opaque.
     * 3. Each leg is drawn with addLegCurve(), then addRaycastMarkers().
     * 4. SRV and OnFoot points are drawn with addModeCurve().
     * 5. Axes are auto-scaled; the title is updated; replot() is called.
     *
     * @param headingBucket  The heading bucket value (rounded heading in degrees).
     * @param legs           All legs for this bucket (from SurveyData::legsForHeading).
     * @param srvPts         SRV points for this bucket.
     * @param footPts        OnFoot points for this bucket.
     */
    void showHeading(int headingBucket,
                     const QVector<Leg>& legs,
                     const QVector<DataPoint>& srvPts,
                     const QVector<DataPoint>& footPts);

    /**
     * @brief Live update — append the latest point of the active leg.
     *
     * Called on every activeLegUpdated() signal from SurveyData (i.e. for
     * every new Ship point while the ship is on the currently selected heading).
     *
     * If the leg's headingBucket does not match m_currentHeading, nothing is
     * done (the ship is on a different heading; no display update needed).
     *
     * A separate live graph (m_activeFwdGraph or m_activeBckGraph) is created
     * on first call and reused thereafter.  Its data is rebuilt from scratch
     * on each call (a cleared + re-added all points in leg.points), which is
     * acceptable because active legs are short.
     *
     * @param leg  The current active leg (may be forward or backward).
     */
    void updateActiveLeg(const Leg& leg);

private slots:
    /**
     * @brief Handle a click on a legend item — toggle the group's visibility.
     *
     * ### Algorithm
     * 1. Cast the clicked item to QCPPlottableLegendItem.
     * 2. Get the underlying plottable (QCPGraph).
     * 3. Check that it is a representative graph in m_groupGraphs.
     * 4. Toggle the representative's visibility.
     * 5. Toggle all member graphs in the group.
     * 6. Dim the legend text (QColor(90,90,90)) when hidden; restore white
     *    when visible.
     * 7. Replot.
     *
     * @param legend  The QCPLegend that was clicked (unused).
     * @param item    The legend item that was clicked.
     * @param event   The mouse event (unused).
     */
    void onLegendClick(QCPLegend* legend, QCPAbstractLegendItem* item, QMouseEvent* event);

    /**
     * @brief Show a hover tooltip for the nearest visible data point.
     *
     * Searches all graphs that have entries in m_graphData.  Only visible
     * graphs are searched (hidden groups are skipped).
     *
     * ### Search algorithm (nearest-neighbour in pixel space)
     * For each (graph, DataPoint) pair:
     *   1. Convert distDeg → pixel X and altitude → pixel Y using the
     *      current axis transforms.
     *   2. Compute squared pixel distance d² = dx² + dy².
     *   3. Track the candidate with the minimum d².
     * Threshold: 18² = 324 px².
     *
     * @param event  Mouse move event containing the cursor position.
     */
    void onMouseMove(QMouseEvent* event);

private:
    /**
     * @brief Reset the plot and recreate the four representative (legend) graphs.
     *
     * Steps:
     *   1. clearGraphs() — removes all QCPGraph objects.
     *   2. Loop over m_markers to remove QCPItemLine / QCPItemText items,
     *      then clear the list.
     *   3. Reset m_activeFwdGraph and m_activeBckGraph to nullptr.
     *   4. Create four representative graphs: "Ship Fwd" (cyan solid),
     *      "Ship Bck" (orange dashed), "SRV" (green solid),
     *      "OnFoot" (yellow dotted).  Each holds an empty QList<QCPGraph*>
     *      in m_groupGraphs.
     */
    void clearPlot();

    /**
     * @brief Draw one ship leg as a scatter-only curve.
     *
     * ### Rendering
     * - No line is drawn (lsNone); only disc markers are used.
     * - Points with altRaycast == false are skipped (unreliable altitude).
     * - Points with altitude ≥ 1200 m are skipped (spike filter).
     * - The disc colour is the group colour (cyan or orange) with opacity
     *   set to @p opacity (older legs are more transparent).
     *
     * ### Group registration
     * After creating the graph, the function scans m_groupGraphs for the
     * representative graph named "Ship Fwd" or "Ship Bck" and appends this
     * data graph to its member list.  This is what enables group toggling.
     *
     * @param leg      The leg to draw.
     * @param opacity  Alpha in [0, 1]; 1 = fully opaque, 0.25 = most transparent.
     */
    void addLegCurve(const Leg& leg, double opacity);

    /**
     * @brief Draw vertical marker lines at altRaycast transition points.
     *
     * ### Algorithm
     * Walk the points in leg.points sequentially.  Track the previous point's
     * altRaycast flag.  Whenever the flag flips (true→false or false→true),
     * draw:
     *   - A QCPItemLine from y = −1e6 to y = +1e6 at x = pt.distDeg.
     *     The line is clipped to the axis rect (setClipToAxisRect(true)) so it
     *     appears as a vertical rule from top to bottom of the plot area.
     *   - A QCPItemText label ("ray→rad" or "rad→ray") at the transition x,
     *     anchored to the bottom-left of the line.
     * Both items are appended to m_markers so they are removed by clearPlot().
     *
     * The item style: magenta dotted pen (QColor(220, 0, 220)), font size 6.
     *
     * @param leg  The ship leg whose altRaycast transitions are to be marked.
     */
    void addRaycastMarkers(const Leg& leg);

    /**
     * @brief Draw SRV or OnFoot points as a scatter curve in the given group.
     *
     * Creates a data graph with lsNone (no line), disc markers of size 3.
     * Points are plotted at (distDeg, altitude).
     * The graph is registered in m_groupGraphs under the named representative,
     * and its full DataPoint list is stored in m_graphData for hover lookup.
     *
     * @param pts       Points to display (all from the same mode).
     * @param col       Marker colour.
     * @param groupName Legend group name ("SRV" or "OnFoot").
     */
    void addModeCurve(const QVector<DataPoint>& pts,
                      const QColor& col, const QString& groupName);

    // ── QCustomPlot objects ───────────────────────────────────────────────────
    QCustomPlot*    m_plot           = nullptr;  ///< The QCustomPlot canvas.
    QCPTextElement* m_title          = nullptr;  ///< Title row above the plot area.

    /**
     * @brief Live-updating forward graph (created on first updateActiveLeg() call
     *        for a forward leg; reset to nullptr by clearPlot()).
     */
    QCPGraph*       m_activeFwdGraph = nullptr;

    /**
     * @brief Live-updating backward graph (same lifecycle as m_activeFwdGraph).
     */
    QCPGraph*       m_activeBckGraph = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────

    /**
     * @brief The heading bucket currently displayed.
     *
     * Used by updateActiveLeg() to decide whether to update the live graph
     * (the incoming leg's bucket must match this value).
     */
    int             m_currentHeading = -1;

    /**
     * @brief Items (lines + labels) placed by addRaycastMarkers().
     *
     * Stored so they can be removed in clearPlot() — QCustomPlot does not
     * automatically remove items when clearGraphs() is called.
     */
    QVector<QCPAbstractItem*> m_markers;

    /**
     * @brief Maps each representative graph to the list of its data graphs.
     *
     * Key:   representative QCPGraph (appears in legend; carries no data).
     * Value: list of data QCPGraph objects that belong to this group.
     *
     * Used by:
     *   - clearPlot() to initialise empty lists.
     *   - addLegCurve() and addModeCurve() to register new data graphs.
     *   - onLegendClick() to toggle all members when the legend is clicked.
     */
    QMap<QCPGraph*, QList<QCPGraph*>> m_groupGraphs;

    /**
     * @brief Maps each data graph to its vector of DataPoints.
     *
     * Used by onMouseMove() to perform the hover nearest-neighbour search.
     * The representative graphs are NOT in this map (they carry no points).
     */
    QMap<QCPGraph*, QVector<DataPoint>> m_graphData;

private:
    /**
     * @brief Apply the dark theme, axis labels, legend, interactions, and title.
     */
    void setupPlot();
};
