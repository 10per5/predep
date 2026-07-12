#include "data/stage.h"

stage_type stage_from_string(const std::string &s)
{
    if (s == "vendor")   return stage_type::vendor;
    if (s == "binary")   return stage_type::binary;
    if (s == "fetch")    return stage_type::fetch;
    if (s == "resource") return stage_type::resource;
    if (s == "run")      return stage_type::run;
    if (s == "docker")   return stage_type::docker;
    if (s == "premake5") return stage_type::premake5;
    if (s == "package")  return stage_type::package;
    if (s == "group")    return stage_type::group;
    if (s == "install")   return stage_type::install;
    if (s == "uninstall") return stage_type::uninstall;
    if (s == "clean")     return stage_type::clean;
    if (s == "copy")      return stage_type::copy;
    if (s == "disabled")  return stage_type::disabled;
    return stage_type::disabled;
}

std::string to_string(stage_type t)
{
    switch (t)
    {
        case stage_type::vendor:   return "vendor";
        case stage_type::binary:   return "binary";
        case stage_type::fetch:    return "fetch";
        case stage_type::resource: return "resource";
        case stage_type::run:      return "run";
        case stage_type::docker:   return "docker";
        case stage_type::premake5: return "premake5";
        case stage_type::package:  return "package";
        case stage_type::group:    return "group";
        case stage_type::disabled: return "disabled";
        case stage_type::install:  return "install";
        case stage_type::uninstall: return "uninstall";
        case stage_type::clean:    return "clean";
        case stage_type::copy:     return "copy";
    }
    return "disabled";
}
