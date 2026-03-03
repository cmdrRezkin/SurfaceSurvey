/**
 * @file MainWindow.h
 * @brief Top-level application window: layout, connections, and event routing.
 *
 * MainWindow owns all UI widgets and the data layer.  It wires signals from
 * CsvTailReader and SurveyData to the appropriate widget slots, and handles
 * the "first point sets the base" logic in onRawPoint().
 */
#pragma once
#include "DataPoint.h"
#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QSplitter>
#include <QtGlobal>

class SurveyData;
class CsvTailReader;
class ScatterWidget;
class ProfileWidget;
class AzimuthWidget;
class RaycastBoundaryWidget;


/**
 * @brief The application's main window.
 *
 * ### Layout
 * @code
 *  ┌────────────────────────────────────────────────────────────────┐
 *  │  [Open CSV]  [File: name.csv]  [Base: lat°, lon°]   [Status]  │ ← toolbar
 *  ├──────────────┬─────────────────────────────────────────────────┤
 *  │              │                                                  │
 *  │  Headings    │   ProfileWidget   (altitude vs dist, by heading) │
 *  │  QListWidget │                                                  │
 *  │              ├─────────────────────────────────────────────────┤
 *  │              │                                                  │
 *  │              │   AzimuthWidget   (altitude vs dist, by azimuth) │
 *  │              │                                                  │
 *  │              ├─────────────────────────────────────────────────┤
 *  │              │                                                  │
 *  │              │   ScatterWidget   (2D lat/lon map)               │
 *  │              │                                                  │
 *  └──────────────┴─────────────────────────────────────────────────┘
 * @endcode
 *
 * ### Data flow and signal routing
 * @code
 * CsvTailReader::newPoint
 *   └──► MainWindow::onRawPoint          // intercepts first point for base
 *            ├── applyBase (once)        // sets base on SurveyData + ScatterWidget
 *            └── SurveyData::addPoint   // feeds the data engine
 *
 * SurveyData::pointAdded
 *   └──► MainWindow::onNewPoint
 *            ├── ScatterWidget::addPoint
 *            └── updateStatusBar
 *
 * SurveyData::newHeadingDetected
 *   └──► MainWindow::onNewHeading        // adds item to the heading list
 *
 * SurveyData::legFinalized
 *   └──► MainWindow::onLegFinalized      // refreshes profile if bucket matches
 *
 * SurveyData::activeLegUpdated
 *   └──► MainWindow::onActiveLegUpdated  // forwards live leg to ProfileWidget
 *
 * QListWidget::itemClicked
 *   └──► MainWindow::onHeadingSelected   // calls refreshProfile()
 * @endcode
 *
 * ### Base coordinate initialisation
 * The survey base is set in one of two ways:
 *   1. **CLI --base lat,lon**: parsed in SurveyDisplay.cpp and passed to the
 *      constructor.  applyBase() is called immediately in the constructor.
 *   2. **First CSV point**: if no --base was given, the first DataPoint emitted
 *      by CsvTailReader triggers applyBase() via onRawPoint(), setting the
 *      first position as the reference.
 *
 * The m_baseSet flag ensures applyBase() is only called once in the auto mode.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief Construct the main window, data layer, and UI.
     *
     * @param numBins   Number of heading bins (default 72 = 5°/bin).
     *                  Passed through to SurveyData::setNumBins().
     * @param baseLat   Latitude of the survey base (qQNaN() = auto-detect).
     * @param baseLon   Longitude of the survey base (qQNaN() = auto-detect).
     * @param parent    Owning QWidget (typically nullptr for a top-level window).
     */
    explicit MainWindow(int numBins = 72,
                        double baseLat = qQNaN(), double baseLon = qQNaN(),
                        QWidget* parent = nullptr);

    /**
     * @brief Open a CSV file for live tail-following.
     *
     * Stops any currently active reader, then starts it on @p path.
     * The CsvTailReader will immediately read any historical data in the file,
     * then continue polling for new lines at 2 Hz.
     *
     * @param path  Absolute path to the CSV file.
     */
    void openCsv(const QString& path);

private slots:
    /**
     * @brief Called for every accepted DataPoint from SurveyData::pointAdded().
     *
     * Forwards the point to the scatter map and updates the status bar.
     *
     * @param pt  DataPoint with distDeg and azimuthDeg already filled.
     */
    void onNewPoint(const DataPoint& pt);

    /**
     * @brief Called when SurveyData sees a heading bucket for the first time.
     *
     * Adds a new item to the heading list widget (m_headingList), then
     * re-sorts the list so buckets appear in ascending numerical order.
     *
     * @param bucket  The new heading bucket value (in degrees).
     */
    void onNewHeading(int bucket);

    /**
     * @brief Called when a ship leg is finalised and stored.
     *
     * If @p bucket matches the currently selected heading (m_selectedHeading),
     * refreshProfile() is called to redraw the profile plot with the now-
     * complete leg included.
     *
     * @param bucket  The heading bucket whose leg was just finalised.
     */
    void onLegFinalized(int bucket);

    /**
     * @brief Called on every new Ship point while a leg is active.
     *
     * If the active leg's bucket matches m_selectedHeading, the live leg is
     * forwarded to ProfileWidget::updateActiveLeg() for a lightweight update
     * (only the live graph is redrawn, not the entire profile).
     */
    void onActiveLegUpdated();

    /**
     * @brief Called when the user clicks a heading in the list.
     *
     * Stores the selected bucket in m_selectedHeading and calls
     * refreshProfile() to display the profile and azimuth-slice for that
     * bucket.
     *
     * @param item  The list widget item that was clicked.
     */
    void onHeadingSelected(QListWidgetItem* item);

    /**
     * @brief Intercepts every raw DataPoint from CsvTailReader.
     *
     * This slot sits between the reader and SurveyData.  Its role:
     *   1. On the first point (m_baseSet == false): call applyBase() using the
     *      point's lat/lon as the survey base, then set m_baseSet = true.
     *   2. Forward every point (including the first) to SurveyData::addPoint().
     *
     * This interception is necessary because SurveyData must have a valid base
     * before it processes the first point — otherwise distDeg and azimuthDeg
     * would be computed relative to the default hardcoded coordinates.
     *
     * @param pt  Raw DataPoint from the CSV (distDeg/azimuthDeg not yet set).
     */
    void onRawPoint(const DataPoint& pt);

    /**
     * @brief Open the file-picker dialog and call openCsv() with the result.
     */
    void onOpenFileClicked();

    /**
     * @brief Update the status label when a file is successfully opened.
     *
     * Trims the path to just the filename for display.
     *
     * @param path  The full path of the opened file.
     */
    void onFileOpened(const QString& path);

private:
    /**
     * @brief Construct all widgets and wire Qt connections.
     *
     * ### Widget hierarchy
     * @code
     * QMainWindow
     * ├── QToolBar
     * │   ├── QAction "Open CSV"
     * │   ├── QLabel m_statusLabel
     * │   └── QLabel m_baseLabel
     * └── central QWidget
     *     └── QHBoxLayout
     *         ├── leftPanel (QWidget)
     *         │   ├── QLabel "Headings"
     *         │   └── m_headingList (QListWidget)
     *         └── rightSplit (QSplitter, Vertical)
     *             ├── m_profile   (ProfileWidget)
     *             ├── m_azimuth   (AzimuthWidget)
     *             └── m_scatter   (ScatterWidget)
     * @endcode
     *
     * Stretch factors: profile 35%, azimuth 35%, scatter 30%.
     */
    void buildUi();

    /**
     * @brief Enforce a 1:1 aspect ratio on the scatter pane after every resize.
     *
     * Sets the scatter pane width equal to the horizontal splitter height so
     * the lat/lon map stays square regardless of window size.
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief Apply base coordinates to all affected objects.
     *
     * Calls setBase() on SurveyData and ScatterWidget (so the base marker moves
     * on the map), and updates the m_baseLabel toolbar text.
     *
     * @param lat  Latitude of the base.
     * @param lon  Longitude of the base.
     */
    void applyBase(double lat, double lon);

    /**
     * @brief Update the status bar with the most recent point's data.
     *
     * Displays: [MODE]  HH:MM:SS  lat=…  lon=…  alt=… m  rc=✓/✗  pts=N
     * (or gravity for OnFoot mode).
     *
     * @param pt  The most recently added DataPoint.
     */
    void updateStatusBar(const DataPoint& pt);

    /**
     * @brief Refresh both profile plots for the given heading bucket.
     *
     * Calls:
     *   - ProfileWidget::showHeading() with legs + SRV/OnFoot points for @p bucket.
     *   - AzimuthWidget::showBucket() with Ship/SRV/OnFoot points by azimuth for @p bucket.
     *
     * @param bucket  The heading bucket to display.
     */
    void refreshProfile(int bucket);

    // ── Data layer ────────────────────────────────────────────────────────────
    SurveyData*    m_data   = nullptr;  ///< Central data store and leg detector.
    CsvTailReader* m_reader = nullptr;  ///< Polls the CSV file for new lines.

    // ── Widgets ───────────────────────────────────────────────────────────────
    ScatterWidget* m_scatter     = nullptr;  ///< 2D lat/lon map.
    ProfileWidget* m_profile     = nullptr;  ///< Altitude vs dist (heading slice).
    AzimuthWidget* m_azimuth     = nullptr;  ///< Altitude vs dist (azimuth slice).
    QListWidget*   m_headingList = nullptr;  ///< List of seen heading buckets.
    QLabel*        m_statusLabel = nullptr;  ///< "Live: filename" in toolbar.
    QLabel*        m_baseLabel   = nullptr;  ///< "Base: lat°, lon°" in toolbar.
    QSplitter*              m_mainSplit      = nullptr;  ///< Horizontal splitter (scatter | right panel).
    RaycastBoundaryWidget*  m_raycastWindow  = nullptr;  ///< Floating window: raycast boundary map.

    // ── State ─────────────────────────────────────────────────────────────────

    /**
     * @brief True once the survey base has been set (via CLI or first point).
     *
     * Prevents onRawPoint() from calling applyBase() more than once when no
     * --base option was given.
     */
    bool           m_baseSet         = false;

    /**
     * @brief The heading bucket currently shown in the profile plots.
     *
     * Set by onHeadingSelected() when the user clicks the list.  Used by
     * onLegFinalized() and onActiveLegUpdated() to decide whether the profile
     * needs updating.  Initialised to −1 (no selection).
     */
    int            m_selectedHeading = -1;

    /** @brief Running count of DataPoints received; shown in the status bar. */
    int            m_pointCount      =  0;
};
