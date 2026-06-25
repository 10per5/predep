#include "sys/download.h"
#include "sys/platform.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <curl/curl.h>

namespace fs = std::filesystem;

namespace download {

struct write_cb_data
{
    std::ofstream *file;
    std::function<void(size_t, size_t)> *progress;
    size_t downloaded;
    size_t total;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *data = static_cast<write_cb_data *>(userdata);
    data->file->write(ptr, static_cast<std::streamsize>(size * nmemb));
    data->downloaded += size * nmemb;
    if (data->progress && data->total)
        (*data->progress)(data->downloaded, data->total);
    return size * nmemb;
}

static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *data = static_cast<write_cb_data *>(userdata);
    std::string header(ptr, size * nmemb);
    std::regex cl_regex("Content-Length:\\s*(\\d+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(header, match, cl_regex))
        data->total = std::stoul(match[1]);
    return size * nmemb;
}

bool http_get(const std::string &url, const std::string &dest_path,
              std::function<void(size_t, size_t)> progress)
{
    auto *curl = curl_easy_init();
    if (!curl)
        return false;

    std::ofstream file(dest_path, std::ios::binary);
    if (!file)
    {
        curl_easy_cleanup(curl);
        return false;
    }

    write_cb_data data{&file, progress ? &progress : nullptr, 0, 0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    auto res = curl_easy_perform(curl);
    file.close();
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool sha256_verify(const std::string &path, const std::string &expected)
{
    auto actual = platform::file_hash(path);
    if (actual.empty())
        return false;
    return actual == expected;
}

bool download_verify(const std::string &url, const std::string &dest,
                     const std::string &expected_hash, int retries,
                     std::function<void(size_t, size_t)> progress)
{
    for (int attempt = 0; attempt <= retries; attempt++)
    {
        if (expected_hash.empty())
        {
            if (!http_get(url, dest, progress))
            {
                if (attempt < retries)
                {
                    auto delay = std::chrono::seconds(1 << attempt);
                    std::cerr << "  retrying in " << (1 << attempt) << "s...\n";
                    std::this_thread::sleep_for(delay);
                }
                continue;
            }
            auto actual = platform::file_hash(dest);
            if (!actual.empty())
                std::cerr << "  info: sha256 = \"" << actual << "\""
                          << "  # add to config to verify " << dest << "\n";
            return true;
        }

        auto part = dest + ".part";
        if (!http_get(url, part, progress))
        {
            fs::remove(part);
            if (attempt < retries)
            {
                auto delay = std::chrono::seconds(1 << attempt);
                std::cerr << "  retrying in " << (1 << attempt) << "s...\n";
                std::this_thread::sleep_for(delay);
            }
            continue;
        }

        if (!sha256_verify(part, expected_hash))
        {
            auto actual = platform::file_hash(part);
            std::cerr << "  ERROR: SHA256 mismatch for " << dest << "\n";
            if (!actual.empty())
                std::cerr << "    expected: " << expected_hash << "\n"
                          << "    actual:   " << actual << "\n"
                          << "    WARNING: verify the download source before updating SHA256\n";
            fs::remove(part);
            return false;
        }

        fs::rename(part, dest);
        return true;
    }
    return false;
}

}
