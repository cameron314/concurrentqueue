// ©2013 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

#include "model-checker/include/threads.h"
#include "corealgo.h"

void thread_main(void* param)
{
	int id = *(int*)param;
	int& dequeueCount = *(int*)param;
	dequeueCount = 0;
	
	int last[4] = { 0 };
	
	enqueue((id << 24) | 1);
	enqueue((id << 24) | 2);
	
	int element;
	bool success = try_dequeue(element);
	if (success) {
		MODEL_ASSERT((element & 0xFFFFFF) > last[element >> 24]);
		last[element >> 24] = element & 0xFFFFFF;
		++dequeueCount;
	}
	success = try_dequeue(element);
	if (success) {
		MODEL_ASSERT((element & 0xFFFFFF) > last[element >> 24]);
		last[element >> 24] = element & 0xFFFFFF;
		++dequeueCount;
	}
}

int user_main(int, char**)
{
	init();
	
	// Start out as thread IDs, but are re-used by the threads
	// to indicate the number of elements each one dequeued
	int w = 1, x = 2, y = 3, z = 4;
	
	thrd_t a, b, c, d;
	
	thrd_create(&a, &thread_main, &w);
	thrd_create(&b, &thread_main, &x);
	thrd_create(&c, &thread_main, &y);
	thrd_create(&d, &thread_main, &z);
	
	thrd_join(a);
	thrd_join(b);
	thrd_join(c);
	thrd_join(d);
	
	MODEL_ASSERT(w + x + y + z + size_approx() == 8);
	
	return 0;
}
