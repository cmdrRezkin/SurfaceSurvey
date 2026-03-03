/**
 * @file MainWindow.cpp
 * @brief Implementation of MainWindow — widget construction, signal wiring,
 *        and event-routing slots.
 */
#include "MainWindow.h"
#include "SurveyData.h"
#include "CsvTailReader.h"
#include "ScatterWidget.h"
#include "ProfileWidget.h"
#include "AzimuthWidget.h"
#include "RaycastBoundaryWidget.h"

#include <QApplication>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QFont>
#include <QResizeEvent>


// =============================================================================
// Constructor
// =============================================================================

/**
 * @brief Construct the data layer, UI, and all signal/slot connections.
 *
 * ### Initialisation order
 * 1. Create SurveyData and configure the bin count.
 * 2. Create CsvTailReader.
 * 3. Build the UI (buildUi()).
 * 4. If --base was given (baseLat/baseLon are not NaN), call applyBase()
 *    immediately so the base is set before any CSV data arrives.
 * 5. Connect SurveyData signals → MainWindow slots.
 * 6. Connect CsvTailReader::newPoint → MainWindow::onRawPoint (intercepts
 *    first point to set base if not already set).
 *
 * ### Why route through onRawPoint instead of connecting directly to SurveyData?
 * SurveyData::addPoint() needs the base coordinates set before the first call
 * so that distDeg / azimuthDeg are computed correctly.  Routing through
 * onRawPoint() lets us set the base from the first CSV point and then forward
 * the point — this is impossible with a direct connection.
 */
MainWindow::MainWindow(int numBins, double baseLat, double baseLon, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Delta 69 — Survey Viewer");
    setMinimumSize(1200, 700);

    m_data   = new SurveyData(this);
    m_data->setNumBins(numBins);
    m_reader = new CsvTailReader(this);

    buildUi();

    // Apply base immediately if the CLI supplied one
    if (!qIsNaN(baseLat) && !qIsNaN(baseLon))
        applyBase(baseLat, baseLon);

    // ── SurveyData → UI ───────────────────────────────────────────────────
    connect(m_data, &SurveyData::pointAdded,
            this, &MainWindow::onNewPoint);
    connect(m_data, &SurveyData::newHeadingDetected,
            this, &MainWindow::onNewHeading);
    connect(m_data, &SurveyData::legFinalized,
            this, &MainWindow::onLegFinalized);
    connect(m_data, &SurveyData::activeLegUpdated,
            this, &MainWindow::onActiveLegUpdated);

    // ── Raycast boundary window ───────────────────────────────────────────
    m_raycastWindow = new RaycastBoundaryWidget(this);
    connect(m_data, &SurveyData::pointAdded,
            m_raycastWindow, &RaycastBoundaryWidget::onNewPoint);
    m_raycastWindow->show();

    // ── CsvTailReader → onRawPoint (interceptor) ──────────────────────────
    connect(m_reader, &CsvTailReader::newPoint,
            this, &MainWindow::onRawPoint);
    connect(m_reader, &CsvTailReader::fileOpened,
            this, &MainWindow::onFileOpened);
    connect(m_reader, &CsvTailReader::parseError,
            [](const QString& line, const QString& reason) {
                Q_UNUSED(line); Q_UNUSED(reason);
                // Currently silent; could be displayed in a log widget
            });
}


// =============================================================================
// UI construction
// =============================================================================

/**
 * @brief Build the toolbar, heading list, and three-pane right panel.
 *
 * ### Right panel vertical split (QSplitter, Qt::Vertical)
 * The three widgets share the right half of the window:
 *   - ProfileWidget  (top)    — stretch factor 35
 *   - AzimuthWidget  (middle) — stretch factor 35
 *   - ScatterWidget  (bottom) — stretch factor 30
 *
 * Stretch factors are relative, so the profile and azimuth plots each get
 * 35/(35+35+30) = 35% of the height, and the scatter map gets 30%.
 */
void MainWindow::buildUi()
{
    // ── Toolbar ───────────────────────────────────────────────────────────
    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    auto* openAct = new QAction(QIcon::fromTheme("document-open"), "Open CSV", this);
    openAct->setShortcut(QKeySequence::Open);
    toolbar->addAction(openAct);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFileClicked);

    toolbar->addSeparator();
    m_statusLabel = new QLabel("No file loaded", this);
    m_statusLabel->setStyleSheet("color: #88aaff; padding: 0 8px;");
    toolbar->addWidget(m_statusLabel);

    toolbar->addSeparator();
    m_baseLabel = new QLabel("Base: auto (first point)", this);
    m_baseLabel->setStyleSheet("color: #aaffaa; padding: 0 8px;");
    toolbar->addWidget(m_baseLabel);

    // ── Central widget: left panel + right splitter ────────────────────────
    auto* central  = new QWidget(this);
    auto* hlay     = new QHBoxLayout(central);
    hlay->setContentsMargins(4, 4, 4, 4);
    hlay->setSpacing(4);
    setCentralWidget(central);

    // Left panel: heading list
    auto* leftPanel = new QWidget(this);
    auto* leftLay   = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    auto* headingLabel = new QLabel("Headings", leftPanel);
    headingLabel->setAlignment(Qt::AlignCenter);
    headingLabel->setStyleSheet("color: white; font-weight: bold; padding: 4px;");
    m_headingList = new QListWidget(leftPanel);
    m_headingList->setMaximumWidth(110);
    m_headingList->setStyleSheet(
        "QListWidget { background: #0a0a1a; color: #ccddff; border: 1px solid #334; }"
        "QListWidget::item:selected { background: #1a2a4a; color: white; }"
        "QListWidget::item:hover { background: #111a30; }"
    );
    leftLay->addWidget(headingLabel);
    leftLay->addWidget(m_headingList);
    leftPanel->setMaximumWidth(120);
    connect(m_headingList, &QListWidget::itemClicked,
            this, &MainWindow::onHeadingSelected);

    // Main horizontal splitter: scatter (left, square) | right vertical splitter
    m_mainSplit = new QSplitter(Qt::Horizontal, this);
    m_mainSplit->setStyleSheet("QSplitter::handle { background: #334; }");

    m_scatter = new ScatterWidget(m_mainSplit);
    m_mainSplit->addWidget(m_scatter);

    // Right vertical splitter: profile (top) + azimuth (bottom)
    auto* rightSplit = new QSplitter(Qt::Vertical, m_mainSplit);
    m_profile = new ProfileWidget(rightSplit);
    m_azimuth = new AzimuthWidget(rightSplit);
    rightSplit->addWidget(m_profile);
    rightSplit->addWidget(m_azimuth);
    rightSplit->setStretchFactor(0, 1);
    rightSplit->setStretchFactor(1, 1);
    rightSplit->setStyleSheet("QSplitter::handle { background: #334; }");

    m_mainSplit->addWidget(rightSplit);
    m_mainSplit->setStretchFactor(0, 0);  // scatter width fixed by resizeEvent
    m_mainSplit->setStretchFactor(1, 1);  // right panel takes remaining space

    hlay->addWidget(leftPanel);
    hlay->addWidget(m_mainSplit, 1);

    setStyleSheet(
        "QMainWindow { background: #080818; }"
        "QToolBar { background: #0e0e28; border-bottom: 1px solid #334; }"
        "QLabel { color: #ccddff; }"
    );
}


// =============================================================================
// resizeEvent — enforce 1:1 aspect ratio on the scatter pane
// =============================================================================

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (!m_mainSplit) return;

    int h = m_mainSplit->height();
    int w = m_mainSplit->width();
    // Clamp so the right panel always gets at least 200 px
    int scatterW = qMin(h, w - 200);
    if (scatterW > 0)
        m_mainSplit->setSizes({scatterW, w - scatterW});
}


// =============================================================================
// File open
// =============================================================================

/**
 * @brief Show a file-picker dialog and open the selected CSV.
 */
void MainWindow::onOpenFileClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open Survey CSV", QString(), "CSV files (*.csv);;All files (*)");
    if (!path.isEmpty())
        openCsv(path);
}

/**
 * @brief Stop the current reader (if any) and start tailing @p path.
 */
void MainWindow::openCsv(const QString& path)
{
    if (m_reader->isRunning()) m_reader->stop();
    m_reader->start(path);
}

/**
 * @brief Update the status label to show the filename of the opened file.
 */
void MainWindow::onFileOpened(const QString& path)
{
    m_statusLabel->setText(QString("Live: %1").arg(path.split('/').last()));
}


// =============================================================================
// Base coordinate management
// =============================================================================

/**
 * @brief Set the survey base and propagate to all consumers.
 *
 * Updates:
 *   - SurveyData: all future distDeg / azimuthDeg computed relative to here.
 *   - ScatterWidget: moves the "★ BASE" marker to (lon, lat).
 *   - m_baseLabel: shows the coordinates in the toolbar.
 *   - m_baseSet: prevents a second auto-set from onRawPoint().
 *
 * @param lat  Latitude of the base.
 * @param lon  Longitude of the base.
 */
void MainWindow::applyBase(double lat, double lon)
{
    m_baseSet = true;
    m_data->setBase(lat, lon);
    m_scatter->setBase(lat, lon);
    m_raycastWindow->setBase(lat, lon);
    m_baseLabel->setText(QString("Base: %1°, %2°")
        .arg(lat, 0, 'f', 4)
        .arg(lon, 0, 'f', 4));
}

/**
 * @brief Intercept each raw point; set base from first point if not yet set.
 *
 * This slot is connected to CsvTailReader::newPoint() rather than connecting
 * the reader directly to SurveyData.  On the first call (m_baseSet == false),
 * applyBase() is called with the point's coordinates.  Every point (including
 * the first) is then forwarded to SurveyData::addPoint().
 *
 * @param pt  Raw point from the CSV (distDeg / azimuthDeg not yet filled).
 */
void MainWindow::onRawPoint(const DataPoint& pt)
{
    if (!m_baseSet)
        applyBase(pt.lat, pt.lon);
    m_data->addPoint(pt);
}


// =============================================================================
// Data event slots
// =============================================================================

/**
 * @brief Forward a new point to the scatter map and update the status bar.
 */
void MainWindow::onNewPoint(const DataPoint& pt)
{
    ++m_pointCount;
    m_scatter->addPoint(pt);
    updateStatusBar(pt);
}

/**
 * @brief Add a new heading bucket entry to the heading list widget.
 *
 * Items are sorted after insertion so the list always shows buckets in
 * ascending numerical order regardless of when they were first observed.
 *
 * The bucket value is stored in Qt::UserRole so that onHeadingSelected()
 * can retrieve the integer without parsing the display text.
 */
void MainWindow::onNewHeading(int bucket)
{
    auto* item = new QListWidgetItem(QString("%1°").arg(bucket));
    item->setData(Qt::UserRole, bucket);
    item->setTextAlignment(Qt::AlignCenter);

    // Insert at the correct position for ascending numerical order.
    // sortItems() sorts lexicographically (0, 10, 100, ...) — not what we want.
    int pos = m_headingList->count();
    for (int i = 0; i < m_headingList->count(); ++i) {
        if (m_headingList->item(i)->data(Qt::UserRole).toInt() > bucket) {
            pos = i;
            break;
        }
    }
    m_headingList->insertItem(pos, item);
}

/**
 * @brief Refresh the profile plots if the finalised leg is for the selected heading.
 */
void MainWindow::refreshProfile(int bucket)
{
    m_profile->showHeading(
        bucket,
        m_data->legsForHeading(bucket),
        m_data->pointsForHeading(bucket, FlightMode::SRV),
        m_data->pointsForHeading(bucket, FlightMode::OnFoot));

    m_azimuth->showBucket(
        bucket,
        m_data->pointsByAzimuth(bucket, FlightMode::Ship),
        m_data->pointsByAzimuth(bucket, FlightMode::SRV),
        m_data->pointsByAzimuth(bucket, FlightMode::OnFoot));
}

/**
 * @brief Called when a leg is finalised — refresh if the bucket matches the selection.
 */
void MainWindow::onLegFinalized(int bucket)
{
    if (bucket == m_selectedHeading)
        refreshProfile(bucket);
}

/**
 * @brief Called on every Ship point — forward the live leg to the profile if matching.
 */
void MainWindow::onActiveLegUpdated()
{
    const Leg& active = m_data->activeLeg();
    if (active.headingBucket == m_selectedHeading)
        m_profile->updateActiveLeg(active);
}

/**
 * @brief Called when the user clicks a heading in the list.
 *
 * Reads the bucket value from Qt::UserRole (stored by onNewHeading()), records
 * it as the selected heading, then calls refreshProfile() to display both the
 * heading-based and azimuth-based profiles for that bucket.
 */
void MainWindow::onHeadingSelected(QListWidgetItem* item)
{
    m_selectedHeading = item->data(Qt::UserRole).toInt();
    refreshProfile(m_selectedHeading);
}


// =============================================================================
// Status bar
// =============================================================================

/**
 * @brief Build and display a status message for the most recent point.
 *
 * ### Format
 * @code
 *   [MODE]  HH:MM:SS  lat=XX.XXXX  lon=YY.YYYY  alt=ZZZm  rc=✓  pts=N
 * @endcode
 * For OnFoot: the altitude field is replaced with g=X.XXX (gravity).
 */
void MainWindow::updateStatusBar(const DataPoint& pt)
{
    QString mode;
    switch (pt.mode) {
    case FlightMode::Ship:   mode = "SHIP";    break;
    case FlightMode::SRV:    mode = "SRV";     break;
    case FlightMode::OnFoot: mode = "ON FOOT"; break;
    default:                  mode = "?";
    }

    QString extra;
    if (pt.mode == FlightMode::OnFoot)
        extra = QString("  g=%1").arg(pt.gravity, 0, 'f', 3);
    else
        extra = QString("  alt=%1m  rc=%2").arg(pt.altitude).arg(pt.altRaycast ? "✓" : "✗");

    statusBar()->showMessage(
        QString("[%1]  %2  lat=%3  lon=%4%5  pts=%6")
            .arg(mode)
            .arg(QString::fromStdString(pt.timestamp.toString("hh:mm:ss").toStdString()))
            .arg(pt.lat, 0, 'f', 4)
            .arg(pt.lon, 0, 'f', 4)
            .arg(extra)
            .arg(m_pointCount)
    );
}
