#include "sys/platform.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
#include <unistd.h>
#include <openssl/evp.h>

namespace fs = std::filesystem;

namespace platform {

static std::string path_expanded(std::string s)
{
    if (s.size() > 0 && s[0] == '~' && (s.size() == 1 || s[1] == '/'))
        if (auto *home = std::getenv("HOME"))
            s = std::string(home) + s.substr(1);
    return s;
}

std::string which(const std::string &cmd)
{
    auto path_env = std::getenv("PATH");
    if (!path_env)
        return {};

    std::string path_str(path_env);
    std::istringstream ss(path_str);
    std::string dir;

    auto with_ext = [&](const std::string &d)
    {
        auto full = d + "/" + cmd;
        if (fs::exists(full) && access(full.c_str(), X_OK) == 0)
            return fs::canonical(full).string();
        return std::string{};
    };

    while (std::getline(ss, dir, ':'))
    {
        auto found = with_ext(dir);
        if (!found.empty())
            return found;
    }
    return {};
}

std::string os()
{
#ifdef _WIN32
    return "windows";
#elif __APPLE__
    return "darwin";
#else
    return "linux";
#endif
}

std::string arch()
{
#if defined(__x86_64__) || defined(_M_AMD64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386";
#else
    return "x86_64";
#endif
}

std::string exe_name(const std::string &base)
{
#ifdef _WIN32
    return base + ".exe";
#else
    return base;
#endif
}

std::string exe_path()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, sizeof(buf));
    return buf;
#elif __APPLE__
    char buf[4096];
    uint32_t size = sizeof(buf);
    _NSGetExecutablePath(buf, &size);
    return buf;
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
    {
        buf[len] = '\0';
        return buf;
    }
    return "";
#endif
}

std::string cache_dir()
{
    std::string dir;
#ifdef _WIN32
    auto *local = std::getenv("LOCALAPPDATA");
    dir = local ? std::string(local) + "/predep" : "~/.cache/predep";
#elif __APPLE__
    auto *env = std::getenv("PREDEP_CACHE");
    dir = env ? std::string(env) : "~/Library/Caches/predep";
#else
    auto *env = std::getenv("PREDEP_CACHE");
    dir = env ? std::string(env) : "~/.cache/predep";
#endif
#ifndef _WIN32
    dir = path_expanded(dir);
#endif
    return dir;
}

bool file_exists(const std::string &path)
{
    return fs::exists(path) && fs::is_regular_file(path);
}

bool dir_exists(const std::string &path)
{
    return fs::exists(path) && fs::is_directory(path);
}

std::string file_hash(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    auto ctx = EVP_MD_CTX_new();
    if (!ctx)
        return {};

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return {};
    }

    char buf[8192];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0)
        EVP_DigestUpdate(ctx, buf, file.gcount());

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return {};
    }

    EVP_MD_CTX_free(ctx);

    std::string result;
    result.reserve(64);
    for (unsigned int i = 0; i < hash_len; i++)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", hash[i]);
        result += hex;
    }
    return result;
}

}
