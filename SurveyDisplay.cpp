/**
 * @file SurveyDisplay.cpp
 * @brief Application entry point — argument parsing, palette, and window launch.
 *
 * This is the only file that contains main().  Its responsibilities are:
 *   1. Create the QApplication and apply the global dark palette.
 *   2. Parse the command-line arguments (--n, --base, positional CSV path).
 *   3. Construct the MainWindow with the parsed configuration.
 *   4. Optionally open a CSV file specified on the command line.
 *   5. Enter the Qt event loop.
 *
 * ### Command-line interface
 * @code
 *   Usage: delta69_viewer [options] [csv]
 *
 *   Options:
 *     -h, --help           Show this help message and exit.
 *     -v, --version        Show version and exit.
 *     -n <bins>            Number of heading bins (default 72 = 5°/bin).
 *     --base <lat,lon>     Reference position (default: first CSV point).
 *
 *   Positional:
 *     csv                  CSV file to open immediately on launch (optional).
 * @endcode
 *
 * ### --n  (number of heading bins)
 * Controls the granularity of heading-bucket grouping throughout the
 * application.  The bin width is 360 / n degrees.  Examples:
 *   | n   | bin width | use case                          |
 *   |-----|-----------|-----------------------------------|
 *   | 36  | 10°       | coarse; large survey areas        |
 *   | 72  | 5°        | default; good balance             |
 *   | 180 | 2°        | fine; dense data on small bodies  |
 *
 * Values outside [1, 360] are silently replaced with 72.  This validation
 * is done both here (before passing to MainWindow) and inside
 * SurveyData::setNumBins() as a defensive double-check.
 *
 * ### --base lat,lon
 * Sets the survey reference position.  If omitted, the first DataPoint
 * received from the CSV file is used as the base (handled in
 * MainWindow::onRawPoint()).
 *
 * Parsing: the value string "lat,lon" is split on ',' and each part is
 * converted to double via QString::toDouble() with an ok flag.  If either
 * conversion fails (non-numeric input), both values stay as qQNaN() and
 * the auto-detect fallback is used.
 */

#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QPalette>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("delta69_viewer");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("CMDR REZKIN");

    // ── Global dark palette ────────────────────────────────────────────────
    // The QSS stylesheets on individual widgets cover most elements.  The
    // Fusion palette fills in anything not explicitly styled (scrollbars,
    // combo-box dropdowns, tool-tips, etc.) so that no bright-white widgets
    // appear unexpectedly.
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(10, 10, 24));    // window background
    dark.setColor(QPalette::WindowText,      Qt::white);              // default text
    dark.setColor(QPalette::Base,            QColor(15, 15, 30));    // input field bg
    dark.setColor(QPalette::AlternateBase,   QColor(20, 20, 40));    // alternate row
    dark.setColor(QPalette::Text,            QColor(200, 220, 255)); // input text
    dark.setColor(QPalette::Button,          QColor(25, 25, 50));    // button bg
    dark.setColor(QPalette::ButtonText,      Qt::white);              // button label
    dark.setColor(QPalette::Highlight,       QColor(30, 80, 160));   // selection bg
    dark.setColor(QPalette::HighlightedText, Qt::white);              // selected text
    app.setPalette(dark);

    // ── Command-line argument parsing ─────────────────────────────────────
    QCommandLineParser parser;
    parser.setApplicationDescription("Delta 69 Survey Viewer — live CSV display");
    parser.addHelpOption();
    parser.addVersionOption();

    // Positional: optional CSV file path
    parser.addPositionalArgument("csv", "CSV file to open (optional)");

    // -n <bins>: number of heading/azimuth bins
    QCommandLineOption nOpt(QString("n"),
        "Number of heading bins (default 72 = 5 deg/bin)", "bins", "72");
    parser.addOption(nOpt);

    // --base lat,lon: survey reference position
    QCommandLineOption baseOpt(QString("base"),
        "Reference position as lat,lon (default: first point in CSV)", "lat,lon");
    parser.addOption(baseOpt);

    parser.process(app);   // parses argv; exits with help/version text if requested

    // ── -n validation ─────────────────────────────────────────────────────
    // toInt() returns 0 on parse failure, which is also out of range.
    int numBins = parser.value(nOpt).toInt();
    if (numBins < 1 || numBins > 360) numBins = 72;

    // ── --base parsing ────────────────────────────────────────────────────
    // Default: qQNaN() signals "use first CSV point" to MainWindow.
    // A qQNaN base is detected with qIsNaN() in MainWindow's constructor.
    double baseLat = qQNaN(), baseLon = qQNaN();
    if (parser.isSet(baseOpt)) {
        const QStringList parts = parser.value(baseOpt).split(',');
        if (parts.size() == 2) {
            bool ok1 = false, ok2 = false;
            double la = parts[0].trimmed().toDouble(&ok1);
            double lo = parts[1].trimmed().toDouble(&ok2);
            if (ok1 && ok2) { baseLat = la; baseLon = lo; }
            // If either conversion failed, baseLat/baseLon stay qQNaN()
            // and the auto-detect fallback is used silently.
        }
    }

    // ── Window creation and launch ────────────────────────────────────────
    MainWindow win(numBins, baseLat, baseLon);
    win.show();

    // Open the CSV file given on the command line, if any.
    // This triggers CsvTailReader::start(), which reads historical data
    // immediately and then continues polling at 2 Hz.
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        win.openCsv(args.first());

    return app.exec();
}
