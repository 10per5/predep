#include "cfg/config.h"
#include <filesystem>
#include <fstream>
#include <tomlpp/toml.hpp>

struct config_node::impl
{
    std::shared_ptr<const toml::table> root;
    const toml::node *node = nullptr;
};

config_node::config_node(std::shared_ptr<const impl> impl)
    : m_impl(std::move(impl))
{}

config_node config_node::from_table(const void *tbl)
{
    auto *t = static_cast<const toml::table*>(tbl);
    auto impl = std::make_shared<config_node::impl>();
    impl->root = std::make_shared<const toml::table>(*t);
    impl->node = impl->root.get();
    return config_node(std::move(impl));
}

config_node config_node::parse_file(const std::string &path)
{
    auto data = std::make_shared<toml::table>(toml::parse_file(path));
    auto impl = std::make_shared<config_node::impl>();
    impl->root = data;
    impl->node = impl->root.get();
    return config_node(std::move(impl));
}

bool config_node::has(const std::string &key) const
{
    if (!m_impl || !m_impl->node)
        return false;
    auto tbl = m_impl->node->as_table();
    return tbl && tbl->contains(key);
}

bool config_node::is_string() const
{
    return m_impl && m_impl->node &&
           m_impl->node->as_string() != nullptr;
}

bool config_node::is_array() const
{
    return m_impl && m_impl->node &&
           m_impl->node->as_array() != nullptr;
}

std::string config_node::as_string() const
{
    if (!m_impl || !m_impl->node)
        return {};
    auto s = m_impl->node->as_string();
    return s ? std::string(**s) : std::string{};
}

std::string config_node::get_string(const std::string &key,
                                     const std::string &def) const
{
    if (!m_impl || !m_impl->node)
        return def;
    auto tbl = m_impl->node->as_table();
    if (!tbl)
        return def;
    auto nv = (*tbl)[key];
    auto v = nv.as_string();
    return v ? std::string(**v) : def;
}

bool config_node::get_bool(const std::string &key, bool def) const
{
    if (!m_impl || !m_impl->node)
        return def;
    auto tbl = m_impl->node->as_table();
    if (!tbl)
        return def;
    auto nv = (*tbl)[key];
    auto v = nv.as_boolean();
    return v ? **v : def;
}

std::vector<config_node> config_node::as_array() const
{
    if (!m_impl || !m_impl->node)
        return {};
    auto arr = m_impl->node->as_array();
    if (!arr)
        return {};
    std::vector<config_node> result;
    for (auto &elem : *arr)
    {
        auto sub = std::make_shared<impl>(impl{m_impl->root, &elem});
        result.push_back(config_node(std::move(sub)));
    }
    return result;
}

std::vector<config_node> config_node::get_array(const std::string &key) const
{
    if (!m_impl || !m_impl->node)
        return {};
    auto tbl = m_impl->node->as_table();
    if (!tbl)
        return {};
    auto nv = (*tbl)[key];
    auto arr = nv.as_array();
    if (!arr)
        return {};
    std::vector<config_node> result;
    for (auto &elem : *arr)
    {
        auto sub = std::make_shared<impl>(impl{m_impl->root, &elem});
        result.push_back(config_node(std::move(sub)));
    }
    return result;
}

config_node config_node::get_table(const std::string &key) const
{
    if (!m_impl || !m_impl->node)
        return {};
    auto tbl = m_impl->node->as_table();
    if (!tbl)
        return {};
    auto nv = (*tbl)[key];
    auto sub_tbl = nv.as_table();
    if (!sub_tbl)
        return {};
    auto sub = std::make_shared<impl>(impl{m_impl->root, sub_tbl});
    return config_node(std::move(sub));
}

void config_node::for_each(
    std::function<void(const std::string&, const config_node&)> fn) const
{
    if (!m_impl || !m_impl->node)
        return;
    auto tbl = m_impl->node->as_table();
    if (!tbl)
        return;
    for (auto &[k, v] : *tbl)
    {
        auto sub = std::make_shared<impl>(impl{m_impl->root, &v});
        config_node cn(std::move(sub));
        fn(std::string(k.str()), cn);
    }
}
