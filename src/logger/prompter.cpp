#include "logger/logger.h"
#include "logger/prompter.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

static console::level to_console(safety_level sl)
{
    switch (sl)
    {
        case safety_level::safe:      return console::level::info;
        case safety_level::warning:   return console::level::warn;
        case safety_level::dangerous: return console::level::error;
        case safety_level::critical:  return console::level::error;
    }
    return console::level::error;
}

static void draw_box(const std::string &title,
                     const std::string &body,
                     console::level cl)
{
    auto &cerr = console::stream();
    auto sep = std::string(44, '=');
    auto lc = console::color(cl);
    auto rs = console::reset();

    cerr << lc << sep << rs << "\n";
    cerr << lc << "  " << title << rs << "\n";
    cerr << lc << sep << rs << "\n";

    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line))
        cerr << console::highlight(line) << "\n";

    cerr << lc << sep << rs << "\n";
}

static bool prompt_yes_no(std::string &error)
{
    auto &cerr = console::stream();
    cerr << console::color("prompt")
         << "  Enter 'yes' to continue: " << console::reset();
    std::string input;
    std::getline(std::cin, input);
    if (input != "yes")
    {
        error = "action rejected by user";
        return false;
    }
    return true;
}

// ---------- InteractivePrompter ----------

bool InteractivePrompter::confirm_or_abort(
    const std::string &title,
    const std::string &body,
    std::string &error,
    safety_level level)
{
    if (level == safety_level::safe)
        return true;

    if (level == safety_level::warning)
    {
        if (console::is_tty())
            draw_box(title, body, to_console(level));
        return true;
    }

    if (!console::is_tty())
    {
        error = "non-interactive mode: use --privileged to override";
        return false;
    }

    auto cl = to_console(level);

    if (level == safety_level::critical)
    {
        draw_box(title, body, cl);
        error = "critical operation requires --privileged";
        return false;
    }

    draw_box(title, body, cl);
    return prompt_yes_no(error);
}

// ---------- PrivilegedPrompter ----------

bool PrivilegedPrompter::confirm_or_abort(
    const std::string &title,
    const std::string &body,
    std::string &error,
    safety_level level)
{
    if (level == safety_level::safe)
        return true;

    if (level == safety_level::warning)
    {
        if (console::is_tty())
            draw_box(title, body, to_console(level));
        return true;
    }

    if (!console::is_tty())
    {
        // Headless: countdown then proceed for dangerous/critical
        privileged_countdown(nullptr);
        return true;
    }

    auto cl = to_console(level);

    if (level == safety_level::dangerous)
    {
        // Countdown then proceed (no prompt)
        draw_box(title, body, cl);
        privileged_countdown(nullptr);
        return true;
    }

    // critical: y/N then countdown
    draw_box(title, body, cl);
    if (!prompt_yes_no(error))
        return false;
    privileged_countdown(nullptr);
    return true;
}

// ---------- NullPrompter ----------

// (base class override returns false)

// ---------- Factory ----------

std::unique_ptr<Prompter> make_prompter(bool privileged)
{
    if (privileged)
        return std::make_unique<PrivilegedPrompter>();
    return std::make_unique<InteractivePrompter>();
}

void privileged_countdown(Logger *logger)
{
    if (logger)
        logger->warn("Running in privileged mode — system paths are accessible.");
    if (!console::is_tty())
        return;

    auto &cerr = console::stream();
    auto lc = console::color(console::level::error);
    auto rs = console::reset();

    for (int i = 2; i >= 1; --i)
    {
        cerr << lc << "\r  Starting in " << i << "... " << rs;
        cerr.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    cerr << "\r                           \r";
    cerr.flush();
}
