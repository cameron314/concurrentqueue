#ifndef CONCURRENT_QUEUE_H
#define CONCURRENT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#if defined(CQ_STATIC) //preferred way
#define CQ_EXPORT
#elif defined(DLL_EXPORT)
#define CQ_EXPORT __declspec(dllexport)
#else
#define CQ_EXPORT __declspec(dllimport)
#endif
#else
/*
need non-windows version here
*/
#endif

#define CQ_OK 0
#define CQ_ERR -1
#define CQ_BAD_HANDLE -2
#define CQ_BAD_VALUE_HANDLE -3
#define CQ_VALUE_NOT_FOUND 1

typedef void* CQHandle;
typedef void* BCQHandle;
typedef void* CQValue;

CQ_EXPORT int cq_create(CQHandle* handle);
CQ_EXPORT int cq_destroy(CQHandle* handle);
CQ_EXPORT int cq_enqueue(CQHandle* handle, CQValue* value);
CQ_EXPORT int cq_dequeue(CQHandle* handle, CQValue* value);

CQ_EXPORT int bcq_create(BCQHandle* handle);
CQ_EXPORT int bcq_destroy(BCQHandle* handle);
CQ_EXPORT int bcq_enqueue(BCQHandle* handle, CQValue* value);
CQ_EXPORT int bcq_dequeue(BCQHandle* handle, CQValue* value);


#ifdef __cplusplus
}
#endif


#endif