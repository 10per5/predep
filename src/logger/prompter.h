#pragma once

#include "data/const.h"
#include "logger/console.h"
#include <memory>
#include <string>

class Logger;

class Prompter
{
public:
    virtual ~Prompter() = default;

    virtual bool confirm_or_abort(const std::string &title,
                                  const std::string &body,
                                  std::string &error,
                                  safety_level level = safety_level::dangerous) = 0;
};

class InteractivePrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &title,
                          const std::string &body,
                          std::string &error,
                          safety_level level = safety_level::dangerous) override;
};

class PrivilegedPrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &title,
                          const std::string &body,
                          std::string &error,
                          safety_level level = safety_level::dangerous) override;
};

class NullPrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &,
                          const std::string &,
                          std::string &,
                          safety_level) override { return false; }
};

std::unique_ptr<Prompter> make_prompter(bool privileged);
void privileged_countdown(Logger *logger);
