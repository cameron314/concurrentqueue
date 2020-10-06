#include "concurrentqueue.h"
#include "../blockingconcurrentqueue.h"

typedef moodycamel::BlockingConcurrentQueue<void*> MoodycamelBCQType, *MoodycamelBCQPtr;

extern "C" {
	
int moodycamel_bcq_create(MoodycamelBCQHandle* handle)
{
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

int moodycamel_bcq_enqueue(MoodycamelBCQHandle handle, MoodycamelValue value)
{
	return reinterpret_cast<MoodycamelBCQPtr>(handle)->enqueue(value) ? 1 : 0;
}

int moodycamel_bcq_wait_dequeue(MoodycamelBCQHandle handle, MoodycamelValue* value)
{
	reinterpret_cast<MoodycamelBCQPtr>(handle)->wait_dequeue(*value);
	return 1;
}

int moodycamel_bcq_try_dequeue(MoodycamelBCQHandle handle, MoodycamelValue* value)
{
	return reinterpret_cast<MoodycamelBCQPtr>(handle)->try_dequeue(*value) ? 1 : 0;
}

}