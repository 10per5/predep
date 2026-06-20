#pragma once

#include <ostream>
#include <string>

namespace console {

enum class level { info, warn, error, debug };

bool is_tty();
bool wants_color();
std::ostream &stream();

// ANSI color codes for each severity level
std::string color(level lvl);

// Named highlights: "header", "bullet", "quote", "bracket", "prompt"
std::string color(const char *style);
std::string reset();

// Apply keyword-based color highlighting to a plain text line.
// Colors quoted text ('...', "...") and bracketed text ([...]).
std::string highlight(const std::string &line);

} // namespace console
