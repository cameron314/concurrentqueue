#include "concurrentqueue.h"
#include "../concurrentqueue.h"

typedef moodycamel::ConcurrentQueue<void*> MoodycamelCQType, *MoodycamelCQPtr;

extern "C" {

int moodycamel_cq_create(MoodycamelCQHandle* handle)
{
	if (handle == nullptr) {
		return 0;
	}
	MoodycamelCQPtr retval = new MoodycamelCQType;
	if (retval == nullptr) {
		return 0;
	}
	*handle = retval;
	return 1;
}

int moodycamel_cq_destroy(MoodycamelCQHandle handle)
{
	delete reinterpret_cast<MoodycamelCQPtr>(handle);
	return 1;
}

int moodycamel_cq_enqueue(MoodycamelCQHandle handle, MoodycamelValue value)
{
	if (handle == nullptr) {
		return 0;
	}
	reinterpret_cast<MoodycamelCQPtr>(handle)->enqueue(value);
	return 1;
}

int moodycamel_cq_try_dequeue(MoodycamelCQHandle handle, MoodycamelValue* value)
{
	if (handle == nullptr) {
		return 0;
	}
	if (value == nullptr) {
		return 0;
	}
	return reinterpret_cast<MoodycamelCQPtr>(handle)->try_dequeue(*value) ? 1 : 0;
}

}
