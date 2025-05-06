#pragma once

#include <unistd.h>
#include <vector>

namespace utils {
    ssize_t xread(int fd, void *buffer, size_t count);

    ssize_t xwrite(int fd, const void *buffer, size_t count);

    std::vector<char> readFile(const char *path);
}
