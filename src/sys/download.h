#pragma once

#include <string>
#include <functional>

namespace download {

bool http_get(const std::string &url, const std::string &dest_path,
              std::function<void(size_t, size_t)> progress = {});
bool sha256_verify(const std::string &path, const std::string &expected);
bool download_verify(const std::string &url, const std::string &dest,
                     const std::string &expected_hash, int retries = 2,
                     std::function<void(size_t, size_t)> progress = {});

}
