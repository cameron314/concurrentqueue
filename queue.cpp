#include "queue.h"
#include "concurrentqueue.h"

typedef moodycamel::ConcurrentQueue<void*> CQType, *CQPtr;

#define TO_OBJECT(x) reinterpret_cast<CQPtr>((*x))

extern "C" {

int cq_create(CQHandle* handle)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	
	try {
		CQPtr retval = new CQType;
		*handle = retval;
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int cq_destroy(CQHandle* handle)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	try {
		CQPtr retval = TO_OBJECT(handle);
		delete retval;
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int cq_enqueue(CQHandle* handle, CQValue* value)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	if (value == nullptr) {
		return CQ_BAD_VALUE_HANDLE;
	}
	try {
		CQPtr retval = TO_OBJECT(handle);
		retval->enqueue(*value);
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int cq_dequeue(CQHandle* handle, CQValue* value)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	if (value == nullptr) {
		return CQ_BAD_VALUE_HANDLE;
	}
	try {
		CQPtr retval = TO_OBJECT(handle);
		bool found = retval->try_dequeue(*value);
		if (found == false) {
			return CQ_VALUE_NOT_FOUND;
		}
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

}
