#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <regex>
#include <ctime>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

// ─────────────────────────────────────────────
// CONSTANTS
// ─────────────────────────────────────────────
static const int DEFAULT_POLL_MS  = 500;
static const int SYNC_INTERVAL_S  = 300;   // 5 minutes

// ─────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────
std::string readFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string extractString(const std::string& json, const std::string& key)
{
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch m;
    if (std::regex_search(json, m, re)) return m[1].str();
    return "";
}

std::string extractNumber(const std::string& json, const std::string& key)
{
    std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+\\.?[0-9]*)");
    std::smatch m;
    if (std::regex_search(json, m, re)) return m[1].str();
    return "";
}

std::time_t parseTimestamp(const std::string& ts)
{
    if (ts.size() < 19) return 0;
    struct tm t = {};
    t.tm_year  = std::stoi(ts.substr(0,  4)) - 1900;
    t.tm_mon   = std::stoi(ts.substr(5,  2)) - 1;
    t.tm_mday  = std::stoi(ts.substr(8,  2));
    t.tm_hour  = std::stoi(ts.substr(11, 2));
    t.tm_min   = std::stoi(ts.substr(14, 2));
    t.tm_sec   = std::stoi(ts.substr(17, 2));
    t.tm_isdst = 0;
#ifdef _WIN32
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
}

std::string extractMode(const std::string& json)
{
    std::string flagsStr = extractNumber(json, "Flags");
    if (flagsStr.empty()) return "unknown";

    uint32_t flags = static_cast<uint32_t>(std::stoul(flagsStr));

    if (flags & (1u << 24)) return "ship";
    if (flags & (1u << 25)) return "fighter";
    if (flags & (1u << 26)) return "srv";
    return "on_foot";   // Flags2 bit0 confirmera
}

bool isAltitudeFromRaycast(const std::string& json)
{
    std::string flagsStr = extractNumber(json, "Flags");
    if (flagsStr.empty()) return false;

    uint32_t flags = static_cast<uint32_t>(std::stoul(flagsStr));
    return !(flags & (1u << 29));  // bit29=0 → raycast → fiable
}

// ─────────────────────────────────────────────
// PROCESS CHECK
// Returns true if EliteDangerous process is running
// ─────────────────────────────────────────────
bool isEliteDangerousRunning()
{
#ifdef _WIN32
    return system("tasklist /FI \"IMAGENAME eq EliteDangerous.exe\" 2>NUL"
                  " | find /I \"EliteDangerous.exe\" >NUL") == 0;
#else
    return system("pgrep EliteDangerous > /dev/null 2>&1") == 0;
#endif
}

// ─────────────────────────────────────────────
// DISK SYNC
// Forces OS to write file buffers to physical disk
// ─────────────────────────────────────────────
void syncToDisk(std::ofstream& f)
{
    // Portable best-effort flush: flush C++ buffers.
    // Getting the underlying file descriptor from std::ofstream is
    // implementation-defined, so we avoid non-portable calls here.
    f.flush();
}

// ─────────────────────────────────────────────
// FILE MANAGER
// Handles periodic close/reopen in append mode
// ─────────────────────────────────────────────
class RotatingFile
{
public:
    RotatingFile(const std::string& path, int syncIntervalS)
        : path_(path)
        , syncIntervalS_(syncIntervalS)
        , lastSync_(std::time(nullptr))
    {
        // Open in append mode — safe if file already exists
        file_.open(path_, std::ios::out | std::ios::app);
        if (!file_.is_open())
            throw std::runtime_error("Cannot open output file: " + path_);
    }

    // Write a line and flush stream buffer
    void writeLine(const std::string& line)
    {
        file_ << line << "\n";
        file_.flush();   // C++ buffer → OS buffer

        // Check if sync interval elapsed
        std::time_t now = std::time(nullptr);
        if (now - lastSync_ >= syncIntervalS_)
            forceSync();
    }

    // Force close + reopen : guarantees disk write + releases OS memory
    void forceSync()
    {
        file_.close();
        file_.open(path_, std::ios::out | std::ios::app);
        if (!file_.is_open())
            throw std::runtime_error("Cannot reopen output file: " + path_);

        lastSync_ = std::time(nullptr);
        std::cerr << "[sync] File flushed to disk at "
                  << lastSync_ << "\n";
    }

    bool isOpen() const { return file_.is_open(); }

private:
    std::string   path_;
    std::ofstream file_;
    int           syncIntervalS_;
    std::time_t   lastSync_;
};

// ─────────────────────────────────────────────
// USAGE
// ─────────────────────────────────────────────
void printUsage(const std::string& prog)
{
    std::cerr
        << "Usage: " << prog << " [options] <status.json>\n"
        << "\n"
        << "Options:\n"
        << "  -o <file>   Write CSV to file (default: stdout)\n"
        << "  -d <ms>     Poll delay in milliseconds (default: "
                          << DEFAULT_POLL_MS << ")\n"
        << "  -s <sec>    Disk sync interval in seconds (default: "
                          << SYNC_INTERVAL_S << ")\n"
        << "\n"
        << "Examples:\n"
        << "  " << prog << " Status.json\n"
        << "  " << prog << " -o flight.csv Status.json\n"
        << "  " << prog << " -o flight.csv -d 1000 -s 120 Status.json\n";
}

// ─────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────
int main(int argc, char* argv[])
{
    std::string statusPath  = "";
    std::string outputPath  = "";
    int         pollMs      = DEFAULT_POLL_MS;
    int         syncIntervalS = SYNC_INTERVAL_S;

    // ── Parse arguments ──
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "\033[196;39mERROR:\033[0m  -o requires a filename\n";
                printUsage(argv[0]); return 1;
            }
            outputPath = argv[++i];
        }
        else if (arg == "-d") {
            if (i + 1 >= argc) {
                std::cerr << "\033[196;39mERROR:\033[0m  -d requires a value in ms\n";
                printUsage(argv[0]); return 1;
            }
            try {
                pollMs = std::stoi(argv[++i]);
                if (pollMs <= 0) throw std::invalid_argument("");
            } catch (...) {
                std::cerr << "\033[196;39mERROR:\033[0m  -d must be a positive integer\n";
                return 1;
            }
        }
        else if (arg == "-s") {
            if (i + 1 >= argc) {
                std::cerr << "\033[196;39mERROR:\033[0m  -s requires a value in seconds\n";
                printUsage(argv[0]); return 1;
            }
            try {
                syncIntervalS = std::stoi(argv[++i]);
                if (syncIntervalS <= 0) throw std::invalid_argument("");
            } catch (...) {
                std::cerr << "\033[196;39mERROR:\033[0m  -s must be a positive integer\n";
                return 1;
            }
        }
        else if (arg[0] == '-') {
            std::cerr << "\033[196;39mERROR:\033[0m  Unknown option '" << arg << "'\n";
            printUsage(argv[0]); return 1;
        }
        else {
            if (!statusPath.empty()) {
                std::cerr << "\033[196;39mERROR:\033[0m  Unexpected argument '" << arg << "'\n";
                printUsage(argv[0]); return 1;
            }
            statusPath = arg;
        }
    }

    if (statusPath.empty())
    {
        throw std::runtime_error("No status.json path provided. Usage: " + std::string(argv[0]) + " [options] <status.json>");
    }

    // ── Setup output ──
    // File mode : use RotatingFile with periodic sync
    // Stdout mode : plain cout, no sync needed (no data loss risk)
    std::unique_ptr<RotatingFile> rotFile;
    std::ostream* out = &std::cout;

    if (!outputPath.empty())
    {
        try {
            rotFile = std::make_unique<RotatingFile>(outputPath, syncIntervalS);
        } catch (const std::exception& e) {
            std::cerr << "\033[196;39mERROR:\033[0m  " << e.what() << "\n";
            return 1;
        }

        std::cerr << "=== Boddy surface Logger ===\n"
                  << "Input  : " << statusPath    << "\n"
                  << "Output : " << outputPath    << "\n"
                  << "Delay  : " << pollMs        << " ms\n"
                  << "Sync   : every " << syncIntervalS << " s\n"
                  << "Press Ctrl+C to stop.\n\n";
    }

    // ── Write CSV header ──
    const std::string header = "timestamp,body,mode,latitude,longitude,altitude,alt_raycast,heading,g,temperature";
    if (rotFile)
        rotFile->writeLine(header);
    else
        *out << header << "\n";

    std::time_t lastTime = 0;

    bool running=true;
    while (running)
    {
        if (!isEliteDangerousRunning()) {
            std::cerr << "\033[41;34m[info]\033[0m Elite Dangerous is not running. \033[32mExiting.\033[0m"<<std::endl;
            running &= false;
        }

        std::string status = readFile(statusPath);

        if (!status.empty())
        {
            std::string ts  = extractString(status, "timestamp");
            std::string lat = extractNumber(status, "Latitude");
            std::string lon = extractNumber(status, "Longitude");
            std::string alt = extractNumber(status, "Altitude");
            std::string hdg = extractNumber(status, "Heading");
            std::string g   = "";
            if(status.find("Gravity") != std::string::npos)
                g += extractNumber(status, "Gravity");
            
            std::string kT   = "";
            if(status.find("Temperature") != std::string::npos)
                kT += extractNumber(status, "Temperature");

            std::string body   = "";
            if(status.find("BodyName") != std::string::npos)
                body += extractNumber(status, "BodyName");

            if (!ts.empty() && !lat.empty() && !lon.empty())
            {
                std::time_t currentTime = parseTimestamp(ts);

                if (currentTime >= lastTime + 1)
                {
                    std::string mode    = extractMode(status);
                    std::string raycast = isAltitudeFromRaycast(status) ? "1" : "0";


                    std::string line = ts  + "," +
                                       body + "," +
                                       mode + "," +
                                       lat + "," +
                                       lon + "," +
                                       alt + "," +
                                       raycast + "," +
                                       hdg + "," +
                                       g + "," +
                                       kT;
                    if (rotFile)
                        rotFile->writeLine(line);
                    else
                        *out << line << "\n";

                    lastTime = currentTime;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }

    return 0;
}