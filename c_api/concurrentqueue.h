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

#define MOODYCAMEL_OK 0
#define MOODYCAMEL_ERR -1
#define MOODYCAMEL_BAD_HANDLE -2
#define MOODYCAMEL_BAD_VALUE_HANDLE -3
#define MOODYCAMEL_VALUE_NOT_FOUND 1

typedef void* CQHandle;
typedef void* BCQHandle;
typedef void* CQValue;

MOODYCAMEL_EXPORT int moodycamel_cq_create(CQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_cq_destroy(CQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_cq_enqueue(CQHandle handle, CQValue value);
MOODYCAMEL_EXPORT int moodycamel_cq_try_dequeue(CQHandle handle, CQValue value);

MOODYCAMEL_EXPORT int moodycamel_bcq_create(BCQHandle* handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_destroy(BCQHandle handle);
MOODYCAMEL_EXPORT int moodycamel_bcq_enqueue(BCQHandle handle, CQValue value);
MOODYCAMEL_EXPORT int moodycamel_bcq_wait_dequeue(BCQHandle handle, CQValue value);


#ifdef __cplusplus
}
#endif

