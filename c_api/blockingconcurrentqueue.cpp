#include "concurrentqueue.h"
#include "../blockingconcurrentqueue.h"

typedef moodycamel::BlockingConcurrentQueue<void*> BCQType, *BCQPtr;

extern "C" {
	
int moodycamel_bcq_create(BCQHandle* handle)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	BCQPtr retval = new BCQType;
	*handle = retval;
	return MOODYCAMEL_OK;
}

int moodycamel_bcq_destroy(BCQHandle handle)
{
	BCQPtr retval = reinterpret_cast<BCQPtr>(handle);
	delete retval;
	return MOODYCAMEL_OK;
}

int moodycamel_bcq_enqueue(BCQHandle handle, CQValue value)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	if (value == nullptr) {
		return MOODYCAMEL_BAD_VALUE_HANDLE;
	}
	reinterpret_cast<BCQPtr>(handle)->enqueue(value);
	return MOODYCAMEL_OK;
}

int moodycamel_bcq_wait_dequeue(BCQHandle handle, CQValue value)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	if (value == nullptr) {
		return MOODYCAMEL_BAD_VALUE_HANDLE;
	}
	reinterpret_cast<BCQPtr>(handle)->wait_dequeue(value);
	return MOODYCAMEL_OK;
}

}