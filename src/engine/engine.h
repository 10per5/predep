#pragma once

#include <memory>
#include <string>
#include <vector>

#include "data/stage.h"

struct runtime;
struct stage_desc;

struct stage_view
{
    std::string name;
    stage_type type;
    std::vector<std::string> depends;
    std::string source_file;
};

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

    // Read-only view of every stage for visualization (audit).
    std::vector<stage_view> stage_views() const;

    bool resolve(const std::string &stage_name, runtime &ctx);
    bool resolve_all(runtime &ctx);

    std::string last_error() const;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};
