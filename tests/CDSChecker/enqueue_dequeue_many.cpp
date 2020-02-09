// ©2013 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

#include "model-checker/include/threads.h"
#include "corealgo.h"

void consumer(void* param)
{
	int id = *(int*)param;
	int& dequeueCount = *(int*)param;
	dequeueCount = 0;
	
	int last = 0;
	
	int element;
	bool success = try_dequeue(element);
	if (success) {
		MODEL_ASSERT(element > last);
		last = element;
		++dequeueCount;
	}
	success = try_dequeue(element);
	if (success) {
		MODEL_ASSERT(element > last);
		last = element;
		++dequeueCount;
	}
}

void producer(void* param)
{
	for (int i = 1; i <= 8; ++i)
		enqueue(i);

	consumer(param);
}

int user_main(int, char**)
{
	init();
	
	// Start out as thread IDs, but are re-used by the threads
	// to indicate the number of elements each one dequeued
	int w = 1, x = 2, y = 3, z = 4;
	
	thrd_t a, b, c, d;
	
	thrd_create(&a, &producer, &w);
	thrd_create(&b, &consumer, &x);
	thrd_create(&c, &consumer, &y);
	thrd_create(&d, &consumer, &z);
	
	thrd_join(a);
	thrd_join(b);
	thrd_join(c);
	thrd_join(d);
	
	MODEL_ASSERT(w + x + y + z + size_approx() == 8);
	
	return 0;
}
