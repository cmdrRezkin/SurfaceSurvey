/**
 * @file CsvTailReader.h
 * @brief Asynchronous CSV tail-follower using a QTimer polling loop.
 *
 * CsvTailReader watches a single CSV file and emits newPoint() for every new
 * line that the Elite Dangerous logger appends.  It works like the Unix
 * `tail -f` command, but implemented with periodic polling rather than
 * OS-level file-change notifications (which are unreliable across different
 * platforms and file systems).
 */
#pragma once
#include "DataPoint.h"
#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QString>


/**
 * @brief Polls a CSV file for new lines and emits parsed DataPoints.
 *
 * ### How tail-following works
 * The reader keeps track of the last byte position it read (`m_lastPos`).
 * Every `pollMs` milliseconds the timer fires poll(), which:
 *   1. Compares the current file size with `m_lastSize`.
 *   2. If the file shrank, re-opens it from the beginning (logger rotation).
 *   3. Seeks to `m_lastPos` and reads all new lines until EOF.
 *   4. Each line is parsed; valid records emit newPoint(), invalid ones emit
 *      parseError().
 *
 * ### File rotation
 * The ED logger closes and re-creates the CSV file every few minutes.  When
 * the new file is empty (or smaller than the previous size), we detect this
 * as a "shrink" and re-open from position 0.
 *
 * ### CSV format
 * The logger produces lines in this column order (7 mandatory + 1 optional):
 * @code
 *   timestamp,mode,latitude,longitude,altitude,alt_raycast,heading[,gravity]
 * @endcode
 * The header line is skipped once on each file open.
 */
class CsvTailReader : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct with an optional Qt parent for memory management.
     * @param parent  Owning QObject.
     */
    explicit CsvTailReader(QObject* parent = nullptr);

    /**
     * @brief Destructor: stops the timer and closes any open file.
     */
    ~CsvTailReader() override;

    /**
     * @brief Open @p csvPath for tailing and start the polling timer.
     *
     * If the file does not exist yet, the timer still starts; poll() will
     * keep retrying to open the file on every tick until it succeeds.
     *
     * Calling start() again while already running replaces the current file
     * (stop() is called implicitly).
     *
     * @param csvPath  Absolute or relative path to the CSV file.
     * @param pollMs   Poll interval in milliseconds (default 500 ms = 2 Hz).
     */
    void start(const QString& csvPath, int pollMs = 500);

    /**
     * @brief Stop the polling timer (does not close the file).
     *
     * The file remains open so that a subsequent start() does not need to
     * re-seek.  Closing the file is handled by the destructor.
     */
    void stop();

    /**
     * @brief Returns true if the polling timer is currently active.
     */
    bool isRunning() const { return m_timer.isActive(); }

signals:
    /**
     * @brief Emitted for every successfully parsed CSV line.
     *
     * Connected to MainWindow::onRawPoint(), which intercepts the first point
     * to set the survey base, then forwards every point to SurveyData::addPoint().
     *
     * @param pt  The parsed and validated DataPoint (distDeg / azimuthDeg not
     *            yet filled — SurveyData does that).
     */
    void newPoint(const DataPoint& pt);

    /**
     * @brief Emitted when a file is successfully opened (or re-opened after rotation).
     * @param path  The absolute path of the file that was opened.
     */
    void fileOpened(const QString& path);

    /**
     * @brief Emitted for any line that could not be parsed into a valid DataPoint.
     *
     * Currently connected to a no-op lambda in MainWindow.  Useful for
     * debugging or future error display.
     *
     * @param line    The raw CSV line that failed parsing.
     * @param reason  Human-readable reason for the failure.
     */
    void parseError(const QString& line, const QString& reason);

private slots:
    /**
     * @brief Called every @p pollMs by the internal QTimer.
     *
     * Handles file rotation detection, header skipping, and line-by-line
     * parsing.  See the class documentation for the full algorithm.
     */
    void poll();

private:
    /**
     * @brief Open (or re-open) the file and reset all position tracking.
     *
     * Closes the file if it is already open, then re-opens it in ReadOnly+Text
     * mode.  Resets m_lastPos, m_lastSize, and m_headerSkipped so that the
     * header will be skipped again on the next poll().
     *
     * @return true if the file was opened successfully.
     */
    bool        openFile();

    /**
     * @brief Parse one CSV line into a DataPoint.
     *
     * ### Field layout (0-indexed columns)
     * | Index | Field       | Type   | Notes                                |
     * |-------|-------------|--------|--------------------------------------|
     * | 0     | timestamp   | string | ISO 8601, parsed with Qt::ISODate    |
     * | 1     | mode        | string | "ship" / "srv" / "on_foot"           |
     * | 2     | latitude    | double | decimal degrees                      |
     * | 3     | longitude   | double | decimal degrees                      |
     * | 4     | altitude    | double | metres                               |
     * | 5     | alt_raycast | int    | "1" = raycast, "0" = radius mode     |
     * | 6     | heading     | int    | degrees 0–359                        |
     * | 7     | gravity     | double | optional, relative to 1 g            |
     *
     * Lines with fewer than 7 fields return a default-constructed DataPoint
     * whose valid() method returns false.
     *
     * @param line  One non-empty, trimmed CSV text line.
     * @return      Parsed DataPoint; check valid() before use.
     */
    DataPoint   parseLine(const QString& line);

    // ── State ─────────────────────────────────────────────────────────────────

    /** @brief Path to the file being tailed. */
    QString     m_path;

    /** @brief The underlying file object. */
    QFile       m_file;

    /** @brief Text stream attached to m_file for line-by-line reading. */
    QTextStream m_stream;

    /** @brief Byte offset of the last position successfully read. */
    qint64      m_lastPos  = 0;

    /**
     * @brief File size at the last poll, used to detect logger rotation.
     *
     * If the new size is smaller than m_lastSize, the file has been replaced.
     */
    qint64      m_lastSize = 0;

    /**
     * @brief True once the header line has been consumed for the current file.
     *
     * Reset to false on every openFile() call so the header is skipped again
     * after a file rotation.
     */
    bool        m_headerSkipped = false;

    /** @brief The timer that drives the poll loop. */
    QTimer      m_timer;
};
