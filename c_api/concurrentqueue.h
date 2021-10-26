#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MOODYCAMEL_EXPORT
#ifdef _WIN32
#if defined(MOODYCAMEL_STATIC) //preferred way
#define MOODYCAMEL_EXPORT
#elif defined(DLL_EXPORT)
#define MOODYCAMEL_EXPORT __declspec(dllexport)
#else
#define MOODYCAMEL_EXPORT __declspec(dllimport)
#endif
#else
#define MOODYCAMEL_EXPORT
#endif
#endif

typedef void* MoodycamelCQHandle;
typedef void* MoodycamelBCQHandle;
typedef void* MoodycamelValue;

MOODYCAMEL_EXPORT int moodycamel_cq_create(MoodycamelCQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_cq_destroy(MoodycamelCQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_cq_enqueue(MoodycamelCQHandle handle, MoodycamelValue value);
MOODYCAMEL_EXPORT int moodycamel_cq_try_dequeue(MoodycamelCQHandle handle, MoodycamelValue* value);
MOODYCAMEL_EXPORT size_t moodycamel_cq_size_approx(MoodycamelCQHandle handle);

MOODYCAMEL_EXPORT int moodycamel_bcq_create(MoodycamelBCQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_destroy(MoodycamelBCQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_enqueue(MoodycamelBCQHandle handle, MoodycamelValue value);
MOODYCAMEL_EXPORT int moodycamel_bcq_wait_dequeue(MoodycamelBCQHandle handle, MoodycamelValue* value);
MOODYCAMEL_EXPORT int moodycamel_bcq_try_dequeue(MoodycamelBCQHandle handle, MoodycamelValue* value);

#ifdef __cplusplus
}
#endif
