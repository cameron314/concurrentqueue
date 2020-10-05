#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#if defined(MOODYCAMEL_STATIC) //preferred way
#define MOODYCAMEL_EXPORT
#elif defined(DLL_EXPORT)
#define MOODYCAMEL_EXPORT __declspec(dllexport)
#else
#define MOODYCAMEL_EXPORT __declspec(dllimport)
#endif
#else
	
#error *nix support is untested
/*
need non-windows version here
*/
#endif

typedef void* MoodycamelCQHandle;
typedef void* MoodycamelBCQHandle;
typedef void* MoodycamelCQValue;

MOODYCAMEL_EXPORT int moodycamel_cq_create(MoodycamelCQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_cq_destroy(MoodycamelCQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_cq_enqueue(MoodycamelCQHandle handle, MoodycamelCQValue value);
MOODYCAMEL_EXPORT int moodycamel_cq_try_dequeue(MoodycamelCQHandle handle, MoodycamelCQValue* value);

MOODYCAMEL_EXPORT int moodycamel_bcq_create(MoodycamelBCQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_destroy(MoodycamelBCQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_enqueue(MoodycamelBCQHandle handle, MoodycamelCQValue value);
MOODYCAMEL_EXPORT int moodycamel_bcq_wait_dequeue(MoodycamelBCQHandle handle, MoodycamelCQValue* value);


#ifdef __cplusplus
}
#endif

