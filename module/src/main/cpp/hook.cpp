#include <android/log.h>
#include <unistd.h>
#include <vector>

#include "zygisk_next_api.h"

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static ZygiskNextAPI api_table;
void* handle;

// backup of old __openat function
static int (*old_openat)(int fd, const char* pathname, int flag, int mode) = nullptr;
// our replacement for __openat function
static int my_openat(int fd, const char* pathname, int flag, int mode) {
    auto r = old_openat(fd, pathname, flag, mode);
    int e = errno;

    auto cp_fd = api_table.connectCompanion(handle);
    int sz;
    if (cp_fd < 0) {
        goto my_openat_finish;
    }
    sz = strlen(pathname);
    TEMP_FAILURE_RETRY(write(cp_fd, &sz, sizeof(sz)));
    TEMP_FAILURE_RETRY(write(cp_fd, pathname, sz));
    close(cp_fd);

my_openat_finish:
    errno = e;
    return r;
}

// this function will be called after all of the main executable's needed libraries are loaded
// and before the entry of the main executable called
void onModuleLoaded(void* self_handle, const struct ZygiskNextAPI* api) {
    // You need to copy the api table if you want to use it after this callback finished
    memcpy(&api_table, api, sizeof(struct ZygiskNextAPI));
    handle = self_handle;

    auto resolver = api_table.newSymbolResolver("libc.so", nullptr);
    if (!resolver) {
        LOGE("create resolver failed");
        return;
    }

    size_t sz;
    auto addr = api_table.symbolLookup(resolver, "__openat", false, &sz);

    api_table.freeSymbolResolver(resolver);

    if (addr == nullptr) {
        LOGE("failed to find __openat");
        return;
    }

    // inline hook netd's openat function
    if (api_table.inlineHook(addr, (void *) my_openat, (void**) &old_openat) == ZN_SUCCESS) {
        LOGI("inline hook success %p", old_openat);
    } else {
        LOGE("inline hook failed");
    }
}

// declaration of the zygisk next module
__attribute__((visibility("default"), unused))
struct ZygiskNextModule zn_module = {
    .target_api_version = ZYGISK_NEXT_API_VERSION_1,
    .onModuleLoaded = onModuleLoaded,
};

static void onCompanionLoaded() {
    LOGI("companion loaded");
}

static void onModuleConnected(int fd) {
    int sz;
    std::vector<char> buf;
    TEMP_FAILURE_RETRY(read(fd, &sz, sizeof(sz)));
    if (sz > 1024 || sz < 0) {
        goto close_fd;
    }
    buf.resize(sz + 1);
    TEMP_FAILURE_RETRY(read(fd, buf.data(), sz));
    buf[sz] = 0;
    LOGI("opened: %s", buf.data());
close_fd:
    close(fd);
}

__attribute__((visibility("default"), unused))
struct ZygiskNextCompanionModule zn_companion_module = {
    .target_api_version = ZYGISK_NEXT_API_VERSION_1,
    .onCompanionLoaded = onCompanionLoaded,
    .onModuleConnected = onModuleConnected,
};
