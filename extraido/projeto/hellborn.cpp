// language: C++, file: jni/hellborn.cpp, target: Android arm64-v8a
#include <jni.h>
#include <pthread.h>
#include <android/log.h>
#include "hellborn.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HELLBORN", __VA_ARGS__)

static void* overlay_main(void*) { start_overlay();     return nullptr; }
static void* esp_main    (void*) { start_esp_thread();  return nullptr; }
static void* aim_main    (void*) { start_aimbot_thread(); return nullptr; }

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("HELLBORN booting");

    // 1) esconde a lib antes de qualquer outra coisa
    stealth_hide_module();

    // 2) dispara as threads independentes
    pthread_t t;
    pthread_create(&t, nullptr, esp_main,     nullptr); pthread_detach(t);
    pthread_create(&t, nullptr, aim_main,     nullptr); pthread_detach(t);
    pthread_create(&t, nullptr, overlay_main, nullptr); pthread_detach(t);

    LOGI("threads up");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_hellborn_Native_init(JNIEnv*, jclass) { /* stub se precisar de init Java */ }