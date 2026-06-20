#pragma once

#include <format>
#include <memory>
#include <string>

class Logger
{
public:
    virtual ~Logger() = default;

    // --- Runtime string interface (unchanged) ---
    virtual void info(const std::string &msg) = 0;
    virtual void warn(const std::string &msg) = 0;
    virtual void error(const std::string &msg) = 0;
    virtual void debug(const std::string &msg) = 0;

    // --- Format-style helpers (compile-time checked via std::format_string) ---
    template<typename... Args>
    void info_fmt(std::format_string<Args...> fmt, Args&&... args)
    {
        info(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void warn_fmt(std::format_string<Args...> fmt, Args&&... args)
    {
        warn(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void error_fmt(std::format_string<Args...> fmt, Args&&... args)
    {
        error(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void debug_fmt(std::format_string<Args...> fmt, Args&&... args)
    {
        debug(std::format(fmt, std::forward<Args>(args)...));
    }
};

class ColorLogger : public Logger
{
    bool m_debug;
public:
    explicit ColorLogger(bool debug = false);
    void info(const std::string &msg) override;
    void warn(const std::string &msg) override;
    void error(const std::string &msg) override;
    void debug(const std::string &msg) override;
};

class MonochromeLogger : public Logger
{
    bool m_debug;
public:
    explicit MonochromeLogger(bool debug = false);
    void info(const std::string &msg) override;
    void warn(const std::string &msg) override;
    void error(const std::string &msg) override;
    void debug(const std::string &msg) override;
};

class MinifiedLogger : public Logger
{
    bool m_debug;
public:
    explicit MinifiedLogger(bool debug = false);
    void info(const std::string &msg) override;
    void warn(const std::string &msg) override;
    void error(const std::string &msg) override;
    void debug(const std::string &msg) override;
};

class NullLogger : public Logger
{
public:
    void info(const std::string &) override {}
    void warn(const std::string &) override {}
    void error(const std::string &) override {}
    void debug(const std::string &) override {}
};

std::unique_ptr<Logger> make_logger(const std::string &format, bool debug = false);
