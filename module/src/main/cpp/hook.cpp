#include <android/log.h>
#include <unistd.h>
#include <vector>
#include <filesystem>

#include "zygisk_api.h"
#include "zygisk_next_api.h"
#include "utils.hpp"

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define DEX_PATH "/data/adb/modules/znmodsample/classes.dex"

class ZNModSample : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // Convert the Java string representing the application name to a C string
        const char *rawName = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!rawName) return;  // Return if we failed to get the string

        std::string name;
        name = rawName;  // Copy the C string into a std::string
        env->ReleaseStringUTFChars(args->nice_name, rawName);  // Release the string resource

        // If the application name is not "com.android.systemui", close the module library and return
        if (name != "com.android.systemui") {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Try to connect to the companion process and obtain a file descriptor (fd)
        int fd = api->connectCompanion();
        if (fd < 0) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library on error
            return;
        }

        // Read the size of the dex data from the companion process
        size_t dexSize = 0;
        if (utils::xread(fd, &dexSize, sizeof(size_t)) < 0 || !dexSize) {
            close(fd);  // Close the file descriptor in case of failure
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Resize the dex vector to hold the dex data based on the read size
        dexVector.resize(dexSize);
        // Read the actual dex data from the companion process into the vector
        if (utils::xread(fd, dexVector.data(), dexSize) < 0) {
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            close(fd);  // Close the file descriptor
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Successfully read the dex data, so close the file descriptor
        close(fd);

        LOGD("Loaded DEX (size=%zu)", dexSize);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (dexVector.empty()) return;

        injectDex();

        dexVector.clear();
        dexVector.shrink_to_fit();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    std::vector<char> dexVector;

    void injectDex() {
        
    }
};

static void companion(int fd) {
    std::vector<char> dex;

    if (std::filesystem::exists(DEX_PATH)) {
        dex = utils::readFile(DEX_PATH);
    }

    size_t dexSize = dex.size();
    utils::xwrite(fd, &dexSize, sizeof(size_t));

    if (dexSize) utils::xwrite(fd, dex.data(), dexSize);
}

REGISTER_ZYGISK_MODULE(ZNModSample)
REGISTER_ZYGISK_COMPANION(companion)
