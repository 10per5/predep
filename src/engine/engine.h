#pragma once

#include <memory>
#include <string>
#include <vector>

struct runtime;
struct stage_desc;

class engine
{
public:
    engine();
    ~engine();

    bool load_toml(const std::string &path);
    bool load_lua(const std::string &path);

    std::vector<std::string> stage_names() const;
    bool has_stage(const std::string &name) const;
    std::string main_stage() const;

    bool resolve(const std::string &stage_name, runtime &ctx);
    bool resolve_all(runtime &ctx);

    std::string last_error() const;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};
