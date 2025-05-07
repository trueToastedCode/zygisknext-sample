#include <android/log.h>
#include <unistd.h>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>

#include "zygisk_api.h"
#include "zygisk_next_api.h"
#include "utils.hpp"

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define DEX_PATH "/data/adb/modules/znmodsample/classes.dex"
#define SYSTEMUI_MARKER "/dev/znmodsample_systemui_done"

bool wasHandled() {
    struct stat buffer;
    bool exists = (stat(SYSTEMUI_MARKER, &buffer) == 0);
    LOGD("Marker file exists: %d", exists);
    return exists;
}

bool setHandled() {
    int fd = open(SYSTEMUI_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        LOGE("Failed to create marker file: %s (errno: %d)", SYSTEMUI_MARKER, errno);
        return false;
    }
    
    // Write a content marker
    const char *content = "1";
    ssize_t result = write(fd, content, 1);
    close(fd);
    
    // Ensure file is readable by all processes
    chmod(SYSTEMUI_MARKER, 0666);
    
    if (result != 1) {
        LOGE("Failed to write to marker file (result: %zd, errno: %d)", result, errno);
        return false;
    }
    
    LOGD("Successfully created marker file");
    return true;
}

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
        auto fd = api->connectCompanion();
        if (fd < 0) {
            LOGE("failed to connect companion");
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library on error
            return;
        }

        // Read the size of the dex data from the companion process
        size_t dexSize = 0;
        if (utils::xread(fd, &dexSize, sizeof(size_t)) < 0 || !dexSize) {
            LOGD("received no dex to inject");
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
        LOGD("invoke System-ClassLoader");
        auto clClass = env->FindClass("java/lang/ClassLoader");
        auto getSystemClassLoader = env->GetStaticMethodID(
            clClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        auto systemClassLoader = env->CallStaticObjectMethod(clClass, getSystemClassLoader);

        if (env->ExceptionCheck()) {
            LOGE("failed to invoke System-ClassLoader");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("make InMemoryDexClassLoader");
        auto dexClClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        auto dexClInit = env->GetMethodID(
            dexClClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        auto buffer = env->NewDirectByteBuffer(
            dexVector.data(), static_cast<jlong>(dexVector.size()));
        auto dexCl = env->NewObject(dexClClass, dexClInit, buffer, systemClassLoader);

        if (env->ExceptionCheck()) {
            LOGE("failed to make InMemoryDexClassLoader");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("load entry point class");
        auto loadClass = env->GetMethodID(
            clClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        auto entryClassName = env->NewStringUTF("de.truetoastedcode.znmodsample.EntryPoint");
        auto entryClassObj = env->CallObjectMethod(dexCl, loadClass, entryClassName);
        auto entryPointClass = (jclass) entryClassObj;

        if (env->ExceptionCheck()) {
            LOGE("failed to load entry point class");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("call entry point init");
        auto entryInit = env->GetStaticMethodID(entryPointClass, "init", "()V");
        env->CallStaticVoidMethod(entryPointClass, entryInit);

        if (env->ExceptionCheck()) {
            LOGE("failed to call entry point init");
            env->ExceptionDescribe();
            env->ExceptionClear();
        }

        env->DeleteLocalRef(entryClassName);
        env->DeleteLocalRef(entryClassObj);
        env->DeleteLocalRef(dexCl);
        env->DeleteLocalRef(buffer);
        env->DeleteLocalRef(dexClClass);
        env->DeleteLocalRef(clClass);

        LOGD("jni memory free");
    }
};

static void companion(int fd) {
    if (wasHandled()) {
        size_t dexSize = 0;
        utils::xwrite(fd, &dexSize, sizeof(size_t));
        close(fd);
        return;
    }

    setHandled();

    std::vector<char> dex;

    if (std::filesystem::exists(DEX_PATH)) {
        dex = utils::readFile(DEX_PATH);
    }

    size_t dexSize = dex.size();
    utils::xwrite(fd, &dexSize, sizeof(size_t));

    if (dexSize) {
        utils::xwrite(fd, dex.data(), dexSize);
    }

    close(fd);
}

REGISTER_ZYGISK_MODULE(ZNModSample)
REGISTER_ZYGISK_COMPANION(companion)
