#include "logger/console.h"
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace console {

bool is_tty()
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
#else
    return isatty(STDERR_FILENO);
#endif
}

bool wants_color()
{
    if (std::getenv("NO_COLOR"))
        return false;
    return is_tty();
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
