#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class config_node
{
public:
    config_node() = default;
    explicit operator bool() const { return m_impl != nullptr; }

    bool has(const std::string &key) const;
    bool is_string() const;
    bool is_array() const;

    std::string as_string() const;
    std::vector<config_node> as_array() const;

    std::string get_string(const std::string &key,
                           const std::string &def = "") const;
    bool get_bool(const std::string &key, bool def = false) const;
    // Accepts TOML boolean, integer 1/0, or string "true"/"false"/"1"/"0"
    bool get_bool_flex(const std::string &key, bool def = false) const;
    std::int64_t get_int(const std::string &key, std::int64_t def = 0) const;

    std::vector<config_node> get_array(const std::string &key) const;
    config_node get_table(const std::string &key) const;
    void for_each(std::function<void(const std::string&, const config_node&)>) const;

    static config_node parse_file(const std::string &path);

private:
    struct impl;
    config_node(std::shared_ptr<const impl> impl);
    static config_node from_table(const void *tbl);
    friend class config_loader;
    std::shared_ptr<const impl> m_impl;
};
