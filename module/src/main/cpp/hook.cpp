#include <android/log.h>
#include <android/dlext.h>
#include <unistd.h>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include "zygisk_api.h"
#include "zygisk_next_api.h"
#include "utils.hpp"
#include "resourceguard.hpp"

#if defined(__aarch64__)
#define ARCH "arm64"
#elif defined(__arm__)
#define ARCH "arm"
#elif defined(__x86_64__)
#define ARCH "x86_64"
#elif defined(__i386__)
#define ARCH "x86"
#else
#define ARCH "unknown"
#endif

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define DEX_PATH "/data/adb/modules/znmodsample/classes.dex"
#define LIB_PATH "/data/adb/modules/znmodsample/lib/%s/libentrypoint_lib.so"
#define SYSTEMUI_MARKER "/dev/znmodsample_systemui_done"

enum CompanionCmd {
    LOAD_DEX_AND_LIB,
    MOD_LIB
};

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

// Send a file descriptor over a socket
bool send_fd(int socket, int fd) {
    struct msghdr msg = {};
    struct iovec iov;
    char buf[1] = {0}; // Dummy data to ensure message is sent

    // Set up the iovec for the dummy data
    iov.iov_base = buf;
    iov.iov_len = 1;

    // Set up the message header
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Allocate space for control message (SCM_RIGHTS)
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    // Set up the control message to send the file descriptor
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    // Send the message
    if (TEMP_FAILURE_RETRY(sendmsg(socket, &msg, 0)) < 0) {
        LOGE("Failed to send file descriptor: %s", strerror(errno));
        return false;
    }

    LOGD("File descriptor sent successfully");
    return true;
}

// Receive a file descriptor from a socket
int recv_fd(int socket) {
    struct msghdr msg = {};
    struct iovec iov;
    char buf[1];

    // Set up the iovec for the dummy data
    iov.iov_base = buf;
    iov.iov_len = 1;

    // Set up the message header
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Allocate space for control message
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    // Receive the message
    if (TEMP_FAILURE_RETRY(recvmsg(socket, &msg, 0)) < 0) {
        LOGE("Failed to receive file descriptor: %s", strerror(errno));
        return -1;
    }

    // Extract the file descriptor from the control message
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        LOGE("No valid file descriptor received");
        return -1;
    }

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    LOGD("File descriptor received: %d", fd);
    return fd;
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
        auto cp_fd = api->connectCompanion();
        if (cp_fd < 0) {
            LOGE("failed to connect companion");
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library on error
            return;
        }

        auto cmd = CompanionCmd::LOAD_DEX_AND_LIB;
        utils::xwrite(cp_fd, &cmd, sizeof(cmd));

        // Read the size of the dex data from the companion process
        size_t dexSize = 0;
        if (utils::xread(cp_fd, &dexSize, sizeof(size_t)) < 0 || !dexSize) {
            LOGD("received no dex to inject");
            close(cp_fd);  // Close the file descriptor in case of failure
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Resize the dex vector to hold the dex data based on the read size
        dexVector.resize(dexSize);

        // Read the actual dex data from the companion process into the vector
        if (utils::xread(cp_fd, dexVector.data(), dexSize) < 0) {
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            close(cp_fd);  // Close the file descriptor
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Read the size of the lib data from the companion process
        size_t libSize = 0;
        if (utils::xread(cp_fd, &libSize, sizeof(size_t)) < 0 || !libSize) {
            LOGD("received no lib to inject");
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            close(cp_fd);  // Close the file descriptor in case of failure
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Resize the lib vector to hold the dex data based on the read size
        libVector.resize(libSize);

        // Read the actual lib data from the companion process into the vector
        if (utils::xread(cp_fd, libVector.data(), libSize) < 0) {
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            libVector.clear();  // Clear the vector in case of an error
            libVector.shrink_to_fit();
            close(cp_fd);  // Close the file descriptor
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        LOGD("Loaded DEX (size=%zu) and LIB (size=%zu)", dexSize, libSize);

        // Create anonymous in-memory file descriptor
        auto fd = static_cast<int>(syscall(SYS_memfd_create, "libentrypoint_lib.so", MFD_CLOEXEC));
        if (fd == -1) {
            LOGE("Failed to create memfd: %s", strerror(errno));
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            libVector.clear();  // Clear the vector in case of an error
            libVector.shrink_to_fit();
            close(cp_fd);  // Close the file descriptor
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }
    
        // Write library data to memfd
        auto written = write(fd, libVector.data(), libVector.size());
        if (written != static_cast<ssize_t>(libVector.size())) {
            LOGE("Failed to write library to memfd: %zd/%zu", written, libVector.size());
            dexVector.clear();  // Clear the vector in case of an error
            dexVector.shrink_to_fit();
            libVector.clear();  // Clear the vector in case of an error
            libVector.shrink_to_fit();
            close(cp_fd);  // Close the file descriptor
            close(fd);
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);  // Close the module library
            return;
        }

        // Use companion to make privileged modifications using the file desciptor
        // cmd = CompanionCmd::MOD_LIB;
        cmd = static_cast<CompanionCmd>(-1);
        utils::xwrite(cp_fd, &cmd, sizeof(cmd));

        // if (!send_fd(cp_fd, fd)) {
        //     LOGE("Failed to send file descriptor to companion");
        //     dexVector.clear();
        //     dexVector.shrink_to_fit();
        //     libVector.clear();
        //     libVector.shrink_to_fit();
        //     close(fd);
        //     close(cp_fd);
        //     api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        //     return;
        // }

        // int lib_mod_ack = -1;
        // utils::xread(cp_fd, &lib_mod_ack, sizeof(lib_mod_ack));

        // if (lib_mod_ack) {
        //     LOGE("Lib mod failure ack: %d", lib_mod_ack);
        //     dexVector.clear();
        //     dexVector.shrink_to_fit();
        //     libVector.clear();
        //     libVector.shrink_to_fit();
        //     close(fd);
        //     close(cp_fd);
        //     api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        //     return;
        // }

        LOGD("Made anonymous in-memory file descriptor");

        // Seek to start and construct /proc/self/fd path
        lseek(fd, 0, SEEK_SET);
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

        // Set up the android_dlextinfo structure
        android_dlextinfo dlextinfo;
        memset(&dlextinfo, 0, sizeof(dlextinfo));
        dlextinfo.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
        dlextinfo.library_fd = fd;

        // Load library using android_dlopen_ext
        LOGD("Attempting android_dlopen_ext with fd %d", fd);
        lib_handle = android_dlopen_ext(path, RTLD_NOW | RTLD_GLOBAL, &dlextinfo);

        if (!lib_handle) {
            LOGE("android_dlopen_ext failed: %s", dlerror());
            dexVector.clear();
            dexVector.shrink_to_fit();
            libVector.clear();
            libVector.shrink_to_fit();
            close(fd);
            close(cp_fd);
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGD("Loaded library successfully!");

        close(fd);
        close(cp_fd);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (dexVector.empty() || libVector.empty() || !lib_handle) return;

        injectDex();

        dexVector.clear();
        dexVector.shrink_to_fit();
        libVector.clear();
        libVector.shrink_to_fit();

        // dlclose(lib_handle);
        // DO NOT dlclose(lib_handle)! Let the OS unload it when the app dies.
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    void *lib_handle;
    std::vector<char> dexVector;
    std::vector<char> libVector;

    void injectDex() {
        enum RefE {
            ClClass,
            SystemClassLoader,
            DexClClass,
            Buffer,
            DexCl,
            EntryClassName,
            EntryClassObj,
            ThreadClass,
            CurrentThread,
            OriginalClassLoader,
            SetContextClassLoaderMethod
        };

        bool needRestoreClassLoader = true;

        auto ref = resourceguard::make_resource_guard(
            [](
                jclass clClass,
                jobject systemClassLoader,
                jclass dexClClass,
                jobject buffer,
                jobject dexCl,
                jstring entryClassName,
                jobject entryClassObj,
                jclass threadClass,
                jobject currentThread,
                jobject originalClassLoader,
                jmethodID setContextClassLoaderMethod,
                bool *needRestoreClassLoader,
                JNIEnv *env
            ) {
                if (*needRestoreClassLoader && currentThread && setContextClassLoaderMethod) {
                    LOGD("Restore original context ClassLoader");
                    env->CallVoidMethod(currentThread, setContextClassLoaderMethod, originalClassLoader);
                }
                if (clClass) env->DeleteLocalRef(clClass);
                if (systemClassLoader) env->DeleteLocalRef(systemClassLoader);
                if (dexClClass) env->DeleteLocalRef(dexClClass);
                if (buffer) env->DeleteLocalRef(buffer);
                if (dexCl) env->DeleteLocalRef(dexCl);
                if (entryClassName) env->DeleteLocalRef(entryClassName);
                if (entryClassObj) env->DeleteLocalRef(entryClassObj);
                if (threadClass) env->DeleteLocalRef(threadClass);
                if (currentThread) env->DeleteLocalRef(currentThread);
                if (originalClassLoader) env->DeleteGlobalRef(originalClassLoader);
                LOGD("injectDex resources released!");
            },
            static_cast<jclass>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jclass>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jstring>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jclass>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jobject>(nullptr),
            static_cast<jmethodID>(nullptr),
            &needRestoreClassLoader,
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

        LOGD("Set the context class loader to the InMemoryDexClassLoader");
        auto threadClass = env->FindClass("java/lang/Thread");
        if (!threadClass) {
            LOGE("Failed to find Thread class");
            return;
        }
        if (ref.try_set<RefE::ThreadClass>(threadClass)) return;

        auto currentThreadMethod = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
        if (!currentThreadMethod) {
            LOGE("Failed to get currentThread method");
            return;
        }

        auto currentThread = env->CallStaticObjectMethod(threadClass, currentThreadMethod);
        if (!currentThread) {
            LOGE("Failed to get current thread");
            return;
        }
        if (ref.try_set<RefE::CurrentThread>(currentThread)) return;

        auto getContextClassLoaderMethod = env->GetMethodID(
            env->GetObjectClass(currentThread), 
            "getContextClassLoader", 
            "()Ljava/lang/ClassLoader;"
        );
        if (!getContextClassLoaderMethod) {
            LOGE("Failed to get getContextClassLoader method");
            return;
        }

        auto setContextClassLoaderMethod = env->GetMethodID(
            env->GetObjectClass(currentThread), 
            "setContextClassLoader", 
            "(Ljava/lang/ClassLoader;)V"
        );
        if (!setContextClassLoaderMethod) {
            LOGE("Failed to get setContextClassLoader method");
            return;
        }
        if (ref.try_set<RefE::SetContextClassLoaderMethod>(setContextClassLoaderMethod)) return;

        auto originalClassLoader = env->CallObjectMethod(currentThread, getContextClassLoaderMethod);
        if (originalClassLoader) {
            originalClassLoader = env->NewGlobalRef(originalClassLoader); // Promote to global ref
            if (ref.try_set<RefE::OriginalClassLoader>(originalClassLoader)) return;
        }

        // Set InMemoryDexClassLoader as the context class loader
        env->CallVoidMethod(currentThread, setContextClassLoaderMethod, dexCl);

        if (env->ExceptionCheck()) {
            LOGE("Exception occurred while setting context class loader");
            env->ExceptionDescribe();
            env->ExceptionClear();
            return;
        }

        // call JNI_OnLoad
        LOGD("Register native code with the JVM");
        using JNI_OnLoadFunc = jint (*)(JavaVM*, void*);
        JNI_OnLoadFunc onLoadFunc = reinterpret_cast<JNI_OnLoadFunc>(dlsym(lib_handle, "JNI_OnLoad"));
        if (!onLoadFunc) {
            LOGE("Failed to find JNI_OnLoad function: %s", dlerror());
            return;
        }

        JavaVM* vm;
        if (env->GetJavaVM(&vm) == JNI_OK) {
            LOGD("Calling JNI_OnLoad (manually)");
            onLoadFunc(vm, nullptr);
        } else {
            LOGE("Failed to get JavaVM from JNIEnv");
            return;
        }

        // restore the original class loader
        LOGD("Restore original context ClassLoader");
        needRestoreClassLoader = false;
        env->CallVoidMethod(currentThread, setContextClassLoaderMethod, originalClassLoader);

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
    auto cmd = static_cast<CompanionCmd>(-1);

    while (1) {
        if (utils::xread(fd, &cmd, sizeof(cmd)) < 0) {
            LOGE("Error receiving command");
            close(fd);
            return;
        }

        LOGD("Received cmd: %d", cmd);

        if (cmd == CompanionCmd::LOAD_DEX_AND_LIB) {
            if (wasHandled()) {
                size_t dexSize = 0;
                utils::xwrite(fd, &dexSize, sizeof(size_t));
                close(fd);
                return;
            }

            setHandled();

            // load dex
            std::vector<char> dex;
            
            if (std::filesystem::exists(DEX_PATH)) {
                dex = utils::readFile(DEX_PATH);
            }

            size_t dexSize = dex.size();
            utils::xwrite(fd, &dexSize, sizeof(size_t));

            if (dexSize) {
                utils::xwrite(fd, dex.data(), dexSize);
            } else {
                close(fd);
                return;
            }

            // load lib
            std::vector<char> lib;

            char libpath[256];
            snprintf(libpath, sizeof(libpath), LIB_PATH, ARCH);

            if (std::filesystem::exists(libpath)) {
                lib = utils::readFile(libpath);
            }

            size_t libSize = lib.size();
            utils::xwrite(fd, &libSize, sizeof(size_t));

            if (libSize) {
                utils::xwrite(fd, lib.data(), libSize);
            }   
        }

        else if (cmd == CompanionCmd::MOD_LIB) {
            // Receive the file descriptor
            int lib_fd = recv_fd(fd);
            if (lib_fd < 0) {
                LOGE("Failed to receive file descriptor");
                int ack = -1;
                utils::xwrite(fd, &ack, sizeof(ack));
                close(fd);
                return;
            }

            // Verify the file descriptor
            struct stat st;
            if (fstat(lib_fd, &st) == 0) {
                LOGD("File exists with size: %llu", static_cast<unsigned long long>(st.st_size));
            } else {
                LOGE("fstat failed: %s", strerror(errno));
                close(lib_fd);
                int ack = -1;
                utils::xwrite(fd, &ack, sizeof(ack));
                close(fd);
                return;
            }

            if (fchmod(lib_fd, 0755) == -1) {
                LOGE("Failed to permission on lib file descriptor");
            } else {
                LOGD("Set permission on lib file descriptor");
            }

            int ack = 0;
            utils::xwrite(fd, &ack, sizeof(ack));

            close(lib_fd);
            close(fd);
            return;
        }

        else {
            LOGE("Received unexpected cmd: %d - terminate now", cmd);
            close(fd);
            return;
        }
    }
}

REGISTER_ZYGISK_MODULE(ZNModSample)
REGISTER_ZYGISK_COMPANION(companion)
