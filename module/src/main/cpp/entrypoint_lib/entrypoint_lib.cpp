#include <jni.h>
#include <android/log.h>
#include "../resourceguard.hpp"

#define LOG_TAG "znmodsample"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

// Remove 'static' and wrap in extern "C" to prevent name mangling
JNIEXPORT void JNICALL nativeMethod(JNIEnv *env, jclass clazz) {
    LOGI("Native method called from EntryPoint class!");
}

static const JNINativeMethod methods[] = {
    {"nativeMethod", "()V", (void *)nativeMethod}
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
        EntryPointClassName,
        EntryClass
    };

    auto ref = resourceguard::make_resource_guard(
        [](
            jclass threadClass,
            jobject currentThread,
            jobject classLoader,
            jclass classLoaderClass,
            jstring entryPointClassName,
            jclass entryClass,
            JNIEnv *env
        ) {
            if (threadClass) env->DeleteLocalRef(threadClass);
            if (currentThread) env->DeleteLocalRef(currentThread);
            if (classLoader) env->DeleteLocalRef(classLoader);
            if (classLoaderClass) env->DeleteLocalRef(classLoaderClass);
            if (entryPointClassName) env->DeleteLocalRef(entryPointClassName);
            if (entryClass) env->DeleteLocalRef(entryClass);
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

    auto entryPointClassName = env->NewStringUTF("de.truetoastedcode.znmodsample.EntryPoint");
    if (ref.try_set<RefE::EntryPointClassName>(entryPointClassName)) return JNI_ERR;

    jclass entryClass = static_cast<jclass>(env->CallObjectMethod(
        classLoader, loadClass, entryPointClassName));
    if (!entryClass) {
        LOGE("Failed to find EntryPoint class in JNI_OnLoad");
        return JNI_ERR;
    }
    if (ref.try_set<RefE::EntryClass>(entryClass)) return JNI_ERR;

    // Register native methods as usual
    if (env->RegisterNatives(entryClass, methods, sizeof(methods)/sizeof(methods[0]))) {
        LOGE("Failed to register natives");
        return JNI_ERR;
    }

    LOGI("JNI_OnLoad completed successfully!");
    return JNI_VERSION_1_6;
}

} // extern "C"