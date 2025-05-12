#include <jni.h>
#include <android/log.h>
#include "../resourceguard.hpp"
#include <sys/sysconf.h>
#include <sys/mman.h>
#include <string>
#include <sys/system_properties.h>
#include "Utils/elf_img.h"
#include <lsplant.hpp>
#include <dobby.h>

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static size_t page_size_;

// Macros to align addresses to page boundaries
#define ALIGN_DOWN(addr, page_size)         ((addr) & -(page_size))
#define ALIGN_UP(addr, page_size)           (((addr) + ((page_size) - 1)) & ~((page_size) - 1))
static pine::ElfImg elf_img;

static void init(int version);

static void init(int version) {
    elf_img.Init("libart.so", version);
}

static bool Unprotect(void *addr) {
    auto addr_uint = reinterpret_cast<uintptr_t>(addr);
    auto page_aligned_prt = reinterpret_cast<void *>(ALIGN_DOWN(addr_uint, page_size_));
    size_t size = page_size_;
    if (ALIGN_UP(addr_uint + page_size_, page_size_) != ALIGN_UP(addr_uint, page_size_)) {
        size += page_size_;
    }

    int result = mprotect(page_aligned_prt, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (result == -1) {
        LOGE("mprotect failed for %p: %s (%d)", addr, strerror(errno), errno);
        return false;
    }
    return true;
}

void *inlineHooker(void *address, void *replacement) {
    if (!Unprotect(address)) {
        return nullptr;
    }

    void *origin_call;
    if (DobbyHook(address, replacement, &origin_call) == RS_SUCCESS) {
        return origin_call;
    } else {
        return nullptr;
    }
}

bool inlineUnHooker(void *originalFunc) {
    return DobbyDestroy(originalFunc) == RT_SUCCESS;
}

extern "C" {

JNIEXPORT jobject JNICALL
doHook(JNIEnv* env, jobject thiz, jobject target, jobject callback) {
    return lsplant::Hook(env, target, thiz, callback);
}

JNIEXPORT jboolean JNICALL
doUnhook(JNIEnv* env, jobject, jobject target) {
    return lsplant::UnHook(env, target);
}

static const JNINativeMethod methods[] = {
    {"doHook", "(Ljava/lang/reflect/Member;Ljava/lang/reflect/Method;)Ljava/lang/reflect/Method;", (void *)doHook},
    {"doUnhook", "(Ljava/lang/reflect/Member;)Z", (void *)doUnhook}
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    enum RefE {
        ThreadClass,
        CurrentThread,
        ClassLoader,
        ClassLoaderClass,
        HookerClassName,
        HookerClass
    };

    auto ref = resourceguard::make_resource_guard(
        [](
            jclass threadClass,
            jobject currentThread,
            jobject classLoader,
            jclass classLoaderClass,
            jstring hookerClassName,
            jclass hookerClass,
            JNIEnv *env
        ) {
            if (threadClass) env->DeleteLocalRef(threadClass);
            if (currentThread) env->DeleteLocalRef(currentThread);
            if (classLoader) env->DeleteLocalRef(classLoader);
            if (classLoaderClass) env->DeleteLocalRef(classLoaderClass);
            if (hookerClassName) env->DeleteLocalRef(hookerClassName);
            if (hookerClass) env->DeleteLocalRef(hookerClass);
            LOGD("JNI_OnLoad resources released!");
        },
        static_cast<jclass>(nullptr),
        static_cast<jobject>(nullptr),
        static_cast<jobject>(nullptr),
        static_cast<jclass>(nullptr),
        static_cast<jstring>(nullptr),
        static_cast<jclass>(nullptr),
        env
    );

    // Use the current thread's context class loader
    jclass threadClass = env->FindClass("java/lang/Thread");
    if (!threadClass) return JNI_ERR;
    if (ref.try_set<RefE::ThreadClass>(threadClass)) return JNI_ERR;

    jmethodID currentThreadMethod = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
    if (!currentThreadMethod) return JNI_ERR;

    jobject currentThread = env->CallStaticObjectMethod(threadClass, currentThreadMethod);
    if (!currentThread) return JNI_ERR;
    if (ref.try_set<RefE::CurrentThread>(currentThread)) return JNI_ERR;

    jmethodID getContextClassLoaderMethod = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getContextClassLoaderMethod) return JNI_ERR;

    jobject classLoader = env->CallObjectMethod(currentThread, getContextClassLoaderMethod);
    if (!classLoader) return JNI_ERR;
    if (ref.try_set<RefE::ClassLoader>(classLoader)) return JNI_ERR;

    // Load the EntryPoint class using the correct class loader
    auto classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if (!classLoaderClass) return JNI_ERR;
    if (ref.try_set<RefE::ClassLoaderClass>(classLoaderClass)) return JNI_ERR;

    auto loadClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) return JNI_ERR;

    auto hookerClassName = env->NewStringUTF("de.truetoastedcode.znmodsample.Hooker");
    if (ref.try_set<RefE::HookerClassName>(hookerClassName)) return JNI_ERR;

    jclass hookerClass = static_cast<jclass>(env->CallObjectMethod(
        classLoader, loadClass, hookerClassName));
    if (!hookerClass) {
        LOGE("Failed to find Hooker class in JNI_OnLoad");
        return JNI_ERR;
    }
    if (ref.try_set<RefE::HookerClass>(hookerClass)) return JNI_ERR;

    // Register native methods as usual
    if (env->RegisterNatives(hookerClass, methods, sizeof(methods)/sizeof(methods[0]))) {
        LOGE("Failed to register natives");
        return JNI_ERR;
    }

    {
        char version_str[128];
        if (!__system_property_get("ro.build.version.sdk", version_str)) {
            LOGE("Failed to obtain SDK int");
            return JNI_ERR;
        }
        long version = std::strtol(version_str, nullptr, 10);

        if (version == 0) {
            LOGE("Invalid SDK int %s", version_str);
            return JNI_ERR;
        }
        init(static_cast<int>(version));
    }

    lsplant::InitInfo initInfo{
        .inline_hooker = inlineHooker,
        .inline_unhooker = inlineUnHooker,
        .art_symbol_resolver = [](std::string_view symbol) -> void * {
            return elf_img.GetSymbolAddress(symbol, false, false);
        },
        .art_symbol_prefix_resolver = [](std::string_view symbol) -> void * {
            return elf_img.GetSymbolAddress(symbol, false, true);
        },
    };
    bool initOk = lsplant::Init(env, initInfo);
    if (!initOk) {
        LOGE("lsplant init failed");
        return JNI_ERR;
    }

    LOGI("JNI_OnLoad completed successfully!");
    return JNI_VERSION_1_6;
}

} // extern "C"
