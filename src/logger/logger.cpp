#include "logger/console.h"
#include "logger/logger.h"
#include <iostream>

// ---- ColorLogger -----------------------------------------------------------

ColorLogger::ColorLogger(bool debug)
    : m_debug(debug)
{}

void ColorLogger::info(const std::string &msg)
{
    std::cout << console::color(console::level::info)
              << " -> " << console::reset() << msg << "\n";
}

void ColorLogger::warn(const std::string &msg)
{
    std::cerr << console::color(console::level::warn)
              << " WARNING: " << console::reset() << msg << "\n";
}

void ColorLogger::error(const std::string &msg)
{
    std::cerr << console::color(console::level::error)
              << "  ERROR: " << console::reset() << msg << "\n";
}

void ColorLogger::debug(const std::string &msg)
{
    if (!m_debug)
        return;
    std::cerr << console::color(console::level::debug)
              << "  [debug] " << console::reset() << msg << "\n";
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
    if (console::wants_color())
        return std::make_unique<ColorLogger>(debug);
    return std::make_unique<MonochromeLogger>(debug);
}
