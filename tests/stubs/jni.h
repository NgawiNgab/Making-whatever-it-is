#pragma once
// Host-build stub for the NDK's <jni.h>.
//
// The mod layer only passes JNI handles around (preloader owns the real work), so
// opaque pointers are enough to type-check it on a desktop. Nothing here is
// ABI-correct: never link this into a real build.
#include <cstdint>
struct _jobject;
using jobject = _jobject*;
using jclass = _jobject*;
using jstring = _jobject*;
using jarray = _jobject*;
using jthrowable = _jobject*;
using jmethodID = const struct _jmethodID*;
using jfieldID = const struct _jfieldID*;
using jint = int32_t;
using jlong = int64_t;
using jboolean = uint8_t;
using jbyte = int8_t;
using jchar = uint16_t;
using jshort = int16_t;
using jfloat = float;
using jdouble = double;
using jsize = jint;
enum jbooleanValues { JNI_FALSE = 0, JNI_TRUE = 1 };
struct JNIEnv;
using JavaVM = void*;
