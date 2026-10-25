#ifndef LOG_CONTROL_H
#define LOG_CONTROL_H

#include <android/log.h>

 #define DISABLE_LOG

#ifdef DISABLE_LOG
    #define LOGI(...) ((void)0)
    #define LOGE(...) ((void)0)
#else
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

#endif
