#pragma once

#include <memory>
#include <string>
#include <vector>

class Prompter
{
public:
    virtual ~Prompter() = default;

    virtual bool confirm_or_abort(const std::string &title,
                                  const std::vector<std::string> &lines,
                                  std::string &error) = 0;
};

class InteractivePrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &title,
                          const std::vector<std::string> &lines,
                          std::string &error) override;
};

class ForcePrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &,
                          const std::vector<std::string> &,
                          std::string &) override { return true; }
};

class NullPrompter : public Prompter
{
public:
    bool confirm_or_abort(const std::string &,
                          const std::vector<std::string> &,
                          std::string &) override { return false; }
};

std::unique_ptr<Prompter> make_prompter(bool force);
