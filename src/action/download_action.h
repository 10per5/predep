#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"
#include <map>
#include <string>
#include <vector>

class download_action : public action
{
    struct vendor_entry_vars
    {
        std::map<std::string, std::string> vars;
        std::string url;
        std::string output_name;
        std::string base;
        std::string fname;
    };

    static vendor_entry_vars resolve_vendor_vars(
        const vendor_entry &ve,
        const std::map<std::string, std::string> &stage_vars,
        const runtime &ctx);

    bool check_vendor_vec(const download_data &d,
                          const std::map<std::string, std::string> &stage_vars,
                          const runtime &ctx) const;
    bool resolve_vendor_vec(download_data &d,
                            const std::map<std::string, std::string> &stage_vars,
                            runtime &ctx,
                            std::string &error,
                            const std::string &type);

public:
    static vendor_entry parse_entry(config_node &elem, const std::string &default_dest);
    static void parse(config_node &cfg, download_data &d);

    bool is_resolved(const stage_desc &sd, runtime &ctx) const override;
    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
