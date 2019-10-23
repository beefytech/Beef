#include <android/log.h>

#define BFP_PRINTF(...) __android_log_print(ANDROID_LOG_INFO, "Beef", __VA_ARGS__)
#define BFP_ERRPRINTF(...) __android_log_print(ANDROID_LOG_ERROR, "Beef", __VA_ARGS__)

#define BFP_HAS_PTHREAD_GETATTR_NP

#include "../posix/PosixCommon.cpp"