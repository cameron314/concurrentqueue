#include "concurrentqueue.h"
#include "../blockingconcurrentqueue.h"

typedef moodycamel::BlockingConcurrentQueue<void*> MoodycamelBCQType, *MoodycamelBCQPtr;

extern "C" {
	
int moodycamel_bcq_create(MoodycamelBCQHandle* handle)
{
	if (handle == nullptr) {
		return 0;
	}
	MoodycamelBCQPtr retval = new MoodycamelBCQType;
	if (retval == nullptr) {
		return 0;
	}
	*handle = retval;
	return 1;
}

int moodycamel_bcq_destroy(MoodycamelBCQHandle handle)
{
	delete reinterpret_cast<MoodycamelBCQPtr>(handle);
	return 1;
}

int moodycamel_bcq_enqueue(MoodycamelBCQHandle handle, MoodycamelCQValue value)
{
	if (handle == nullptr) {
		return 0;
	}
	reinterpret_cast<MoodycamelBCQPtr>(handle)->enqueue(value);
	return 1;
}

int moodycamel_bcq_wait_dequeue(MoodycamelBCQHandle handle, MoodycamelCQValue* value)
{
	if (handle == nullptr) {
		return 0;
	}
	if (value == nullptr) {
		return 0;
	}
	reinterpret_cast<MoodycamelBCQPtr>(handle)->wait_dequeue(*value);
	return 1;
}

}