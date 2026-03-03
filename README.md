# Elite Dangerous — Surface Survey Tools

Two companion programs for recording and visualising planetary surface survey data from Elite Dangerous.

---

## Tools

### `surfaceMonitoring` — telemetry logger

A lightweight CLI daemon that polls Elite Dangerous's `Status.json` file and writes a timestamped CSV of position and telemetry data.  No GUI, no dependencies beyond the C++ standard library.

**What it records**

| Field | Description |
|---|---|
| `timestamp` | UTC timestamp (ISO 8601) |
| `body` | Body name (when available) |
| `mode` | `ship`, `srv`, or `on_foot` |
| `latitude` / `longitude` | Decimal degrees |
| `altitude` | Metres above terrain |
| `alt_raycast` | `1` = raycast measurement (reliable), `0` = orbital-radius estimate |
| `heading` | Compass heading in degrees |
| `g` | Surface gravity (on-foot mode) |
| `temperature` | Surface temperature in K (on-foot mode) |

The tool exits automatically when the Elite Dangerous process is no longer detected.

**Usage**

```
surfaceMonitoring [options] <Status.json>

Options:
  -o <file>   Write CSV to file (default: stdout)
  -d <ms>     Poll interval in milliseconds (default: 500)
  -s <sec>    Disk sync interval in seconds (default: 300)

Examples:
  surfaceMonitoring Status.json
  surfaceMonitoring -o survey.csv Status.json
  surfaceMonitoring -o survey.csv -d 1000 -s 120 Status.json
```

---

### `surfaceViewer` — live survey visualiser

A Qt6 GUI application that reads the CSV produced by `surfaceMonitoring` (live-tailing it while the logger runs) and displays several synchronised views of the survey data.

**Views**

- **Scatter map** — 2D lat/lon plot coloured by flight mode (ship / SRV / on-foot), with a base-position marker.  Maintains a 1:1 aspect ratio.
- **Profile plot** — altitude vs. angular distance from the survey base for the heading currently selected in the heading list.  Forward and backward legs are colour-coded (cyan / orange), SRV points in green, on-foot in yellow.  Older passes are drawn more transparently.  Raycast/radius altitude transitions are marked with vertical lines.  Clicking a legend entry hides or shows its group.
- **Azimuth plot** — shows the same data sliced by geometric bearing from the base rather than compass heading.
- **Raycast boundary window** — a separate window that highlights where the game switches between raycast and radius altitude measurement.

**Heading list**

The left panel lists every compass heading bucket seen in the data.  Clicking one updates the profile and azimuth plots for that bearing.  The bin width is configurable: with 72 bins (default) each bucket covers 5°.

**Leg detection**

Ship-mode data is automatically segmented into *legs* — contiguous runs with a stable heading and a consistent direction of travel (toward or away from the survey reference point).  Direction is determined by linear regression on the last four distance samples and must be confirmed by three consecutive agreeing samples before a leg is split.

**Usage**

```
surfaceViewer [options] [csv]

Options:
  -n <bins>          Number of heading bins (default: 72 = 5°/bin)
  --base <lat,lon>   Survey reference position (default: first CSV point)

Positional:
  csv                CSV file to open on launch (optional; can also use File > Open)
```

A CSV file can also be opened at any time via the toolbar.

---

## Building

### Dependencies

| Dependency | Required by |
|---|---|
| C++23 compiler (Clang / GCC / MSVC) | both |
| CMake 3.16+ | both |
| [Qt6](https://www.qt.io/) (Widgets, PrintSupport) | `surfaceViewer` |
| [QCustomPlot](https://www.qcustomplot.com/) 2.x | `surfaceViewer` |

**QCustomPlot** is not a system package.  Download `qcustomplot.h` and `qcustomplot.cpp` from [qcustomplot.com](https://www.qcustomplot.com/index.php/download) and place them in the repository root before building.  CMake will report an error with the download URL if they are missing.

### Build

```sh
cmake -B build -S .
cmake --build build
```

The executables are placed in `build/`.

---

## Workflow

1. Start a survey session in Elite Dangerous.
2. Run `surfaceMonitoring` to begin logging:
   ```sh
   ./surfaceMonitoring -o survey.csv /path/to/Status.json
   ```
3. Open `surfaceViewer` and load the same CSV (or pass it on the command line):
   ```sh
   ./surfaceViewer survey.csv
   ```
   The viewer tails the file and updates in real time as new points arrive.
4. Select a heading in the list to see the altitude profile for that bearing.
5. `surfaceMonitoring` exits automatically when the game closes.
