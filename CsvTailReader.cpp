/**
 * @file CsvTailReader.cpp
 * @brief Implementation of the CSV tail-following reader.
 */
#include "CsvTailReader.h"
#include <QFileInfo>

// =============================================================================
// Constructor / destructor
// =============================================================================

/**
 * @brief Wire the internal timer to the poll() slot.
 *
 * The timer is not started here; start() does that.
 */
CsvTailReader::CsvTailReader(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &CsvTailReader::poll);
}

/**
 * @brief Stop the timer and close the file.
 *
 * Calling stop() before closing ensures no poll() is in flight when the file
 * handle is released.
 */
CsvTailReader::~CsvTailReader()
{
    stop();
    if (m_file.isOpen()) m_file.close();
}


// =============================================================================
// Public interface
// =============================================================================

/**
 * @brief Open the file and start the polling timer.
 *
 * All position state is reset so we will process the entire file from the
 * beginning (minus the header line).  This is intentional: if the user opens
 * a partially-written CSV, they get all historical data first, then live
 * updates as new lines arrive.
 */
void CsvTailReader::start(const QString& csvPath, int pollMs)
{
    m_path          = csvPath;
    m_lastPos       = 0;
    m_lastSize      = 0;
    m_headerSkipped = false;

    if (openFile())
        emit fileOpened(m_path);

    m_timer.start(pollMs);
}

/**
 * @brief Stop the polling timer (file stays open).
 */
void CsvTailReader::stop()
{
    m_timer.stop();
}


// =============================================================================
// File open / reopen
// =============================================================================

/**
 * @brief Open the file from the beginning.
 *
 * Closes any previously open handle first.  Attaches a QTextStream for
 * convenient line-by-line reading.  Resets all position state so that poll()
 * treats this as a fresh file.
 *
 * @return true on success, false if the file could not be opened.
 */
bool CsvTailReader::openFile()
{
    if (m_file.isOpen()) m_file.close();

    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_stream.setDevice(&m_file);
    m_lastPos       = 0;
    m_lastSize      = m_file.size();
    m_headerSkipped = false;
    return true;
}


// =============================================================================
// Poll — called every pollMs milliseconds
// =============================================================================

/**
 * @brief Read any new lines from the tailed file and emit parsed points.
 *
 * ### Algorithm
 *
 * **Case 1 — File not open (initial open failed or never called):**
 * Try openFile() again.  This lets us recover transparently if the file
 * appears after the viewer was launched.
 *
 * **Case 2 — File shrank (logger rotation):**
 * The ED logger creates a new file every few minutes.  When the current file
 * size drops below the recorded size, we assume the file was replaced and
 * re-open from the beginning.
 *
 * **Case 3 — No new data:**
 * If the current file position equals the file size, there is nothing to read.
 * Return early to avoid a wasted readLine() call.
 *
 * **Case 4 — New data available:**
 * Skip the CSV header line on the first pass.  Then read every available line,
 * parse it, and emit newPoint() or parseError() as appropriate.
 */
void CsvTailReader::poll()
{
    if (!m_file.isOpen()) {
        if (openFile())
            emit fileOpened(m_path);
        return;
    }

    qint64 currentSize = m_file.size();

    // ── Logger rotation detection ──────────────────────────────────────────
    // A shrinking file means the logger closed and recreated it.
    if (currentSize < m_lastSize) {
        openFile();
        emit fileOpened(m_path);
    }
    m_lastSize = currentSize;

    // ── Nothing new to read ────────────────────────────────────────────────
    if (m_file.pos() == currentSize) return;

    // ── Skip header line on first read after each open ────────────────────
    // The first line of the CSV is a column-name header; we do not want to
    // try parsing it as data.
    if (!m_headerSkipped) {
        m_stream.readLine();        // consume and discard the header
        m_headerSkipped = true;
        m_lastPos = m_file.pos();
    }

    // ── Read and parse all new lines ──────────────────────────────────────
    while (!m_stream.atEnd()) {
        QString line = m_stream.readLine().trimmed();
        m_lastPos = m_file.pos();

        if (line.isEmpty()) continue;

        DataPoint pt = parseLine(line);
        if (pt.valid())
            emit newPoint(pt);
        else
            emit parseError(line, "invalid fields");
    }
}


// =============================================================================
// CSV line parser
// =============================================================================

/**
 * @brief Parse one CSV data line into a DataPoint.
 *
 * ### Column layout
 * @code
 *   [0] timestamp  — ISO 8601, e.g. "2024-05-01T13:45:22"
 *   [2] mode       — "ship" | "srv" | "on_foot"
 *   [3] latitude   — decimal degrees (double)
 *   [4] longitude  — decimal degrees (double)
 *   [5] altitude   — metres (double), 0 for on_foot
 *   [6] alt_raycast — "1" if raycast, "0" if radius-mode
 *   [7] heading    — integer degrees 0–359
 *   [8] gravity    — optional, relative g (double), on_foot only
 * @endcode
 *
 * ### Validation
 * Lines with fewer than 7 fields return a default DataPoint, which will fail
 * valid() and be discarded by the caller.
 *
 * The mode string is converted via modeFromString(); if it is unrecognised,
 * mode is set to FlightMode::Unknown, which also fails valid().
 *
 * @param line  One non-empty, trimmed text line from the CSV (no newline).
 * @return      Parsed DataPoint; caller must check valid() before use.
 */
DataPoint CsvTailReader::parseLine(const QString& line)
{
    const QStringList f = line.split(',');
    if (f.size() < 8) return {};    // too few fields — return invalid point (now 8 minimum)

    DataPoint pt;
    pt.timestamp   = QDateTime::fromString(f[0].trimmed(), Qt::ISODate);
    pt.body        = f[1].toDouble();
    pt.mode        = modeFromString(f[2].trimmed());
    pt.lat         = f[3].toDouble();
    pt.lon         = f[4].toDouble();
    pt.altitude    = f[5].toDouble();
    pt.altRaycast  = f[6].trimmed() == "1";
    pt.heading     = f[7].toInt();
    pt.gravity     = (f.size() >= 9) ? f[8].toDouble() : 0.0;
    pt.temperature = (f.size() >= 10) ? f[9].toDouble() : 0.0;

    return pt;
}
