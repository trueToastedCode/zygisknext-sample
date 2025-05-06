#include "utils.hpp"

namespace utils {
    // Hilfsfunktion zum Lesen von Daten
    ssize_t xread(int fd, void *buffer, size_t count) {
        ssize_t total = 0;
        char *buf = static_cast<char *>(buffer);
        while (count > 0) {
            ssize_t ret = TEMP_FAILURE_RETRY(read(fd, buf, count));
            if (ret < 0) return -1;
            buf += ret;
            total += ret;
            count -= ret;
        }
        return total;
    }

    ssize_t xwrite(int fd, const void *buffer, size_t count) {
        ssize_t total = 0;
        char *buf = (char *) buffer;
        while (count > 0) {
            ssize_t ret = TEMP_FAILURE_RETRY(write(fd, buf, count));
            if (ret < 0) return -1;
            buf += ret;
            total += ret;
            count -= ret;
        }
        return total;
    }

    // Hilfsfunktion zum Lesen einer Datei
    std::vector<char> readFile(const char *path) {
        FILE *file = fopen(path, "rb");

        if (!file) return {};

        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        std::vector<char> vector(size);
        fread(vector.data(), 1, size, file);

        fclose(file);

        return vector;
    }
}
