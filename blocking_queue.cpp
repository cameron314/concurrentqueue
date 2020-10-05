#include "queue.h"
#include "blockingconcurrentqueue.h"

typedef moodycamel::BlockingConcurrentQueue<void*> BCQType, *BCQPtr;

#define TO_OBJECT(x) reinterpret_cast<BCQPtr>((*x))

extern "C" {
	
int bcq_create(BCQHandle* handle)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	try {
		BCQPtr retval = new BCQType;
		*handle = retval;
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int bcq_destroy(BCQHandle* handle)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	try {
		BCQPtr retval = TO_OBJECT(handle);
		delete retval;
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int bcq_enqueue(BCQHandle* handle, CQValue* value)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	if (value == nullptr) {
		return CQ_BAD_VALUE_HANDLE;
	}
	try {
		BCQPtr retval = TO_OBJECT(handle);
		retval->enqueue(*value);
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

int bcq_dequeue(BCQHandle* handle, CQValue* value)
{
	if (handle == nullptr) {
		return CQ_BAD_HANDLE;
	}
	if (value == nullptr) {
		return CQ_BAD_VALUE_HANDLE;
	}
	try {
		BCQPtr retval = TO_OBJECT(handle);
		retval->wait_dequeue(*value);
	} catch(...) {
		return CQ_ERR;
	}
	return CQ_OK;
}

}