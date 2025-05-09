#include <android/log.h>
#include <unistd.h>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>

#include "zygisk_api.h"
#include "zygisk_next_api.h"
#include "utils.hpp"
#include "resourceguard.hpp"

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
    void *buffer = nullptr;

    void injectDex() {
        enum RefE {
            ClClass,
            SystemClassLoader,
            DexClClass,
            Buffer,
            DexCl,
            EntryClassName,
            EntryClassObj
        };

        auto ref = resourceguard::make_resource_guard(
            [](
                jclass clClass,
                jobject systemClassLoader,
                jclass dexClClass,
                jobject buffer,
                jobject dexCl,
                jstring entryClassName,
                jobject entryClassObj,
                JNIEnv *env
            ) {
                if (clClass) env->DeleteLocalRef(clClass);
                if (systemClassLoader) env->DeleteLocalRef(systemClassLoader);
                if (dexClClass) env->DeleteLocalRef(dexClClass);
                if (buffer) env->DeleteLocalRef(buffer);
                if (dexCl) env->DeleteLocalRef(dexCl);
                if (entryClassName) env->DeleteLocalRef(entryClassName);
                if (entryClassObj) env->DeleteLocalRef(entryClassObj);
                LOGD("JNI resources released!");
            },
            static_cast<jclass>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jclass>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jstring>(nullptr),
            static_cast<jobject>(nullptr),
            env
        );

        LOGD("Invoke System-ClassLoader");
        auto clClass = env->FindClass("java/lang/ClassLoader");
        if (ref.try_set<RefE::ClClass>(clClass)) return;
        auto getSystemClassLoader = env->GetStaticMethodID(
            clClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        auto systemClassLoader = env->CallStaticObjectMethod(clClass, getSystemClassLoader);
        if (ref.try_set<RefE::SystemClassLoader>(systemClassLoader)) return;

        if (env->ExceptionCheck()) {
            LOGE("Failed to invoke System-ClassLoader");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("Make InMemoryDexClassLoader");
        auto dexClClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        if (ref.try_set<RefE::DexClClass>(dexClClass)) return;
        auto dexClInit = env->GetMethodID(
            dexClClass, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        auto buffer = env->NewDirectByteBuffer(
            dexVector.data(), static_cast<jlong>(dexVector.size()));
        if (ref.try_set<RefE::Buffer>(buffer)) return;
        auto dexCl = env->NewObject(dexClClass, dexClInit, buffer, systemClassLoader);
        if (ref.try_set<RefE::DexCl>(dexCl)) return;

        if (env->ExceptionCheck()) {
            LOGE("Failed to make InMemoryDexClassLoader");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("Load entry point class");
        auto loadClass = env->GetMethodID(
            clClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        auto entryClassName = env->NewStringUTF("de.truetoastedcode.znmodsample.EntryPoint");
        if (ref.try_set<RefE::EntryClassName>(entryClassName)) return;
        auto entryClassObj = env->CallObjectMethod(dexCl, loadClass, entryClassName);
        if (ref.try_set<RefE::EntryClassObj>(entryClassObj)) return;

        if (env->ExceptionCheck()) {
            LOGE("Failed to load entry point class");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("Call entry point init");
        auto entryInit = env->GetStaticMethodID(static_cast<jclass>(entryClassObj), "init", "()V");
        env->CallStaticVoidMethod(static_cast<jclass>(entryClassObj), entryInit);

        if (env->ExceptionCheck()) {
            LOGE("Failed to call entry point init");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        LOGD("DEX injected!");
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
