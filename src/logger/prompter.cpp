#include "logger/prompter.h"
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

static bool is_stdin_tty()
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
#else
    return isatty(STDIN_FILENO);
#endif
}

bool InteractivePrompter::confirm_or_abort(
    const std::string &title,
    const std::vector<std::string> &lines,
    std::string &error)
{
    if (!is_stdin_tty())
    {
        error = "non-interactive mode: use --force to override";
        return false;
    }

    auto &cerr = std::cerr;
    auto sep = std::string(44, '=');

    cerr << sep << "\n";
    cerr << "  " << title << "\n";
    cerr << sep << "\n";
    for (auto &line : lines)
        cerr << line << "\n";
    cerr << sep << "\n";
    cerr << "  Enter 'yes' to continue: ";

    std::string input;
    std::getline(std::cin, input);
    if (input != "yes")
    {
        error = "action rejected by user";
        return false;
    }
    return true;
}

std::unique_ptr<Prompter> make_prompter(bool force)
{
    // --force is declared but behavior not yet implemented.
    // Future: return force ? std::make_unique<ForcePrompter>()
    //                      : std::make_unique<InteractivePrompter>();
    (void)force;
    return std::make_unique<InteractivePrompter>();
}
