#include "logger/logger.h"
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

// ---- helpers ----------------------------------------------------------------

static bool is_tty()
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) && (mode & 0x0004);
#else
    return isatty(STDOUT_FILENO) || isatty(STDERR_FILENO);
#endif
}

// ---- ColorLogger -----------------------------------------------------------

ColorLogger::ColorLogger(bool debug)
    : m_debug(debug)
{}

void ColorLogger::info(const std::string &msg)
{
    std::cout << "\033[36m -> \033[0m" << msg << "\n";
}

void ColorLogger::warn(const std::string &msg)
{
    std::cerr << "\033[33m WARNING: \033[0m" << msg << "\n";
}

void ColorLogger::error(const std::string &msg)
{
    std::cerr << "\033[31m  ERROR: \033[0m" << msg << "\n";
}

void ColorLogger::debug(const std::string &msg)
{
    if (!m_debug)
        return;
    std::cerr << "\033[90m  [debug] \033[0m" << msg << "\n";
}

// ---- MonochromeLogger ------------------------------------------------------

MonochromeLogger::MonochromeLogger(bool debug)
    : m_debug(debug)
{}

void MonochromeLogger::info(const std::string &msg)
{
    std::cout << " -> " << msg << "\n";
}

void MonochromeLogger::warn(const std::string &msg)
{
    std::cerr << " WARNING: " << msg << "\n";
}

void MonochromeLogger::error(const std::string &msg)
{
    std::cerr << "  ERROR: " << msg << "\n";
}

void MonochromeLogger::debug(const std::string &msg)
{
    if (!m_debug)
        return;
    std::cerr << "  [debug] " << msg << "\n";
}

// ---- MinifiedLogger --------------------------------------------------------

MinifiedLogger::MinifiedLogger(bool debug)
    : m_debug(debug)
{}

void MinifiedLogger::info(const std::string &msg)
{
    std::cout << msg << "\n";
}

void MinifiedLogger::warn(const std::string &msg)
{
    std::cerr << "WARNING: " << msg << "\n";
}

void MinifiedLogger::error(const std::string &msg)
{
    std::cerr << "ERROR: " << msg << "\n";
}

void MinifiedLogger::debug(const std::string &msg)
{
    if (!m_debug)
        return;
    std::cerr << msg << "\n";
}

// ---- factory ----------------------------------------------------------------

std::unique_ptr<Logger> make_logger(const std::string &format, bool debug)
{
    if (format == "none")
        return std::make_unique<NullLogger>();
    if (format == "minified")
        return std::make_unique<MinifiedLogger>(debug);
    if (format == "mono")
        return std::make_unique<MonochromeLogger>(debug);

    // auto-detect
    if (!std::getenv("NO_COLOR") && is_tty())
        return std::make_unique<ColorLogger>(debug);
    return std::make_unique<MonochromeLogger>(debug);
}
