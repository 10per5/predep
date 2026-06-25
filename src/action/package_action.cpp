#include "action/package_action.h"
#include "logger/logger.h"
#include "sys/process.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void package_action::parse(config_node &cfg, package_data &d)
{
    d.bundle = cfg.get_string("bundle", "release");

    auto arr = cfg.get_array("artifacts");
    for (auto &elem : arr)
    {
        artifact_entry ae;
        ae.source = elem.get_string("source");
        ae.dest = elem.get_string("dest");
        if (ae.dest.empty())
        {
            auto slash = ae.source.rfind('/');
            ae.dest = (slash != std::string::npos)
                ? ae.source.substr(slash + 1)
                : ae.source;
        }
        d.artifacts.push_back(ae);
    }
}

bool package_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *pkg = dynamic_cast<package_data*>(sd.data.get());
    if (!pkg)
    {
        error = "stage " + sd.name + " has no package data";
        return false;
    }

    auto dist = ctx.resolve_path("root://dist");
    fs::create_directories(dist);

    if (ctx.logger)
        ctx.logger->info("Assembling package in " + dist);

    for (auto &art : pkg->artifacts)
    {
        auto src = ctx.resolve_path(art.source);
        auto dst = (fs::path(dist) / art.dest).string();

        if (!fs::exists(src))
        {
            error = "artifact not found: " + src;
            return false;
        }

        if (fs::is_directory(src))
        {
            bool empty = true;
            for (auto &_ : fs::directory_iterator(src))
            { empty = false; break; }
            if (empty)
            {
                error = "artifact directory is empty: " + src;
                return false;
            }
        }
        else
        {
            if (fs::file_size(src) == 0)
            {
                error = "artifact file is empty: " + src;
                return false;
            }
        }

        fs::create_directories(fs::path(dst).parent_path());

        if (fs::is_directory(src))
        {
            auto dst_dir = dst;
            fs::create_directories(dst_dir);
            for (auto &entry : fs::recursive_directory_iterator(src))
            {
                auto rel = fs::relative(entry.path(), src);
                auto target = (fs::path(dst_dir) / rel).string();
                if (entry.is_directory())
                    fs::create_directories(target);
                else
                    fs::copy(entry.path(), target, fs::copy_options::overwrite_existing);
            }
        }
        else
        {
            fs::copy(src, dst, fs::copy_options::overwrite_existing);
        }

        if (ctx.logger)
            ctx.logger->info(src + " -> " + dst);
    }

    auto fmt = ctx.target_os == "windows" ? "zip" : "tar.gz";

    auto parent = fs::absolute(dist).parent_path().string();
    auto dirname = fs::path(dist).filename().string();
    auto bundle = pkg->bundle.empty() ? dirname : pkg->bundle;

    if (bundle != dirname)
    {
        auto bundled = (fs::path(parent) / bundle).string();
        if (fs::exists(bundled))
            fs::remove_all(bundled);
        fs::rename(dist, bundled);
        dist = bundled;
        dirname = bundle;
    }

    auto archive_path = (fs::path(parent) / bundle).string();

    if (fmt == "zip")
    {
        archive_path += ".zip";
        if (ctx.logger)
            ctx.logger->info("Creating " + archive_path);
#ifdef _WIN32
        auto rc = process::run("powershell", {
            "Compress-Archive", "-Path", dirname,
            "-DestinationPath", archive_path, "-Force"
        }, parent);
#else
        auto rc = process::run("zip", {"-r", archive_path, dirname}, parent);
#endif
        if (rc != 0)
        {
            error = "failed to create zip archive";
            return false;
        }
    }
    else
    {
        archive_path += ".tar.gz";
        if (ctx.logger)
            ctx.logger->info("Creating " + archive_path);
        auto rc = process::run("tar", {"-czf", archive_path, dirname}, parent);
        if (rc != 0)
        {
            error = "failed to create tar.gz archive";
            return false;
        }
    }

    if (ctx.logger)
        ctx.logger->info("Package created: " + archive_path);
    return true;
}
