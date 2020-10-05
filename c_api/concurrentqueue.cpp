#include "concurrentqueue.h"
#include "../concurrentqueue.h"

typedef moodycamel::ConcurrentQueue<void*> CQType, *CQPtr;

extern "C" {

int moodycamel_cq_create(CQHandle* handle)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	CQPtr retval = new CQType;
	*handle = retval;
	return MOODYCAMEL_OK;
}

int moodycamel_cq_destroy(CQHandle handle)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	CQPtr retval = reinterpret_cast<CQPtr>(handle);
	delete retval;
	return MOODYCAMEL_OK;
}

int moodycamel_cq_enqueue(CQHandle handle, CQValue value)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	if (value == nullptr) {
		return MOODYCAMEL_BAD_VALUE_HANDLE;
	}
	reinterpret_cast<CQPtr>(handle)->enqueue(value);
	return MOODYCAMEL_OK;
}

int moodycamel_cq_try_dequeue(CQHandle handle, CQValue value)
{
	if (handle == nullptr) {
		return MOODYCAMEL_BAD_HANDLE;
	}
	if (value == nullptr) {
		return MOODYCAMEL_BAD_VALUE_HANDLE;
	}
	bool found = reinterpret_cast<CQPtr>(handle)->try_dequeue(value);
	if (found == false) {
		return MOODYCAMEL_VALUE_NOT_FOUND;
	}
	return MOODYCAMEL_OK;
}

}
