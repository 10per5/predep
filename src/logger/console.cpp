#include "logger/console.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
  #endif
#else
  #include <unistd.h>
#endif

namespace console {

bool is_tty()
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        return true;
    // Fallback: TERM indicates an ANSI-capable terminal
    // (Git Bash sets TERM=cygwin, WSL sets TERM=xterm-256color, etc.)
    const char *term = std::getenv("TERM");
    return term && *term && std::strcmp(term, "dumb") != 0;
#else
    return isatty(STDERR_FILENO);
#endif
}

bool wants_color()
{
    if (std::getenv("NO_COLOR"))
        return false;

#ifdef _WIN32
    // Enable virtual terminal processing so ANSI escape codes work
    // in modern Windows consoles (10+) natively
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
        {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            return true;
        }
    }
    // Fallback: check TERM for ANSI-capable terminal (Git Bash, etc.)
    const char *term = std::getenv("TERM");
    if (term && *term && std::strcmp(term, "dumb") != 0)
        return true;
    return false;
#else
    return is_tty();
#endif
}

std::ostream &stream()
{
    return std::cerr;
}

std::string color(level lvl)
{
    if (!wants_color())
        return {};
    switch (lvl)
    {
        case level::info:  return "\033[36m";
        case level::warn:  return "\033[33m";
        case level::error: return "\033[31m";
        case level::debug: return "\033[90m";
    }
    return {};
}

std::string color(const char *style)
{
    if (!wants_color())
        return {};
    switch (style[0])
    {
        case 'h': return "\033[1;33m"; // header  – bold yellow
        case 'q': return "\033[32m";   // quote   – green
        case 'b': return "\033[33m";   // bracket – yellow
        case 'p': return "\033[1;32m"; // prompt  – bold green
        case 'u': return "\033[36m";   // bullet  – cyan
    }
    return {};
}

std::string reset()
{
    if (!wants_color())
        return {};
    return "\033[0m";
}

std::string highlight(const std::string &line)
{
    if (!wants_color())
        return line;

    std::string out;
    for (size_t i = 0; i < line.size(); )
    {
        if (line[i] == '\'')
        {
            out += color("quote");
            out += '\'';
            ++i;
            while (i < line.size() && line[i] != '\'')
                out += line[i++];
            if (i < line.size())
            {
                out += '\'';
                ++i;
            }
            out += reset();
        }
        else if (line[i] == '"')
        {
            out += color("quote");
            out += '"';
            ++i;
            while (i < line.size() && line[i] != '"')
                out += line[i++];
            if (i < line.size())
            {
                out += '"';
                ++i;
            }
            out += reset();
        }
        else if (line[i] == '[')
        {
            out += color("bracket");
            out += '[';
            ++i;
            while (i < line.size() && line[i] != ']')
                out += line[i++];
            if (i < line.size())
            {
                out += ']';
                ++i;
            }
            out += reset();
        }
        else if (line[i] == '*')
        {
            out += color("bullet");
            out += '*';
            ++i;
            out += reset();
        }
        else
        {
            out += line[i++];
        }
    }
    return out;
}

} // namespace console
