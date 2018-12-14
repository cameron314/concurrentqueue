# Samples for moodycamel::ConcurrentQueue

Here are some example usage scenarios with sample code. Note that most
use the simplest version of each available method for demonstration purposes,
but they can all be adapted to use tokens and/or the corresponding bulk methods for
extra speed.


## Hello queue
```C++
ConcurrentQueue<int> q;

for (int i = 0; i != 123; ++i)
	q.enqueue(i);

int item;
for (int i = 0; i != 123; ++i) {
	q.try_dequeue(item);
	assert(item == i);
}
```

## Hello concurrency

Basic example of how to use the queue from multiple threads, with no
particular goal (i.e. it does nothing, but in an instructive way).
```C++
ConcurrentQueue<int> q;
int dequeued[100] = { 0 };
std::thread threads[20];

// Producers
for (int i = 0; i != 10; ++i) {
	threads[i] = std::thread([&](int i) {
		for (int j = 0; j != 10; ++j) {
			q.enqueue(i * 10 + j);
		}
	}, i);
}

// Consumers
for (int i = 10; i != 20; ++i) {
	threads[i] = std::thread([&]() {
		int item;
		for (int j = 0; j != 20; ++j) {
			if (q.try_dequeue(item)) {
				++dequeued[item];
			}
		}
	});
}

// Wait for all threads
for (int i = 0; i != 20; ++i) {
	threads[i].join();
}

// Collect any leftovers (could be some if e.g. consumers finish before producers)
int item;
while (q.try_dequeue(item)) {
	++dequeued[item];
}

// Make sure everything went in and came back out!
for (int i = 0; i != 100; ++i) {
	assert(dequeued[i] == 1);
}
```

## Bulk up

Same as previous example, but runs faster.
```C++
ConcurrentQueue<int> q;
int dequeued[100] = { 0 };
std::thread threads[20];

// Producers
for (int i = 0; i != 10; ++i) {
	threads[i] = std::thread([&](int i) {
		int items[10];
		for (int j = 0; j != 10; ++j) {
			items[j] = i * 10 + j;
		}
		q.enqueue_bulk(items, 10);
	}, i);
}

// Consumers
for (int i = 10; i != 20; ++i) {
	threads[i] = std::thread([&]() {
		int items[20];
		for (std::size_t count = q.try_dequeue_bulk(items, 20); count != 0; --count) {
			++dequeued[items[count - 1]];
		}
	});
}

// Wait for all threads
for (int i = 0; i != 20; ++i) {
	threads[i].join();
}

// Collect any leftovers (could be some if e.g. consumers finish before producers)
int items[10];
std::size_t count;
while ((count = q.try_dequeue_bulk(items, 10)) != 0) {
	for (std::size_t i = 0; i != count; ++i) {
		++dequeued[items[i]];
	}
}

// Make sure everything went in and came back out!
for (int i = 0; i != 100; ++i) {
	assert(dequeued[i] == 1);
}
```

## Producer/consumer model (simultaneous)

In this model, one set of threads is producing items,
and the other is consuming them concurrently until all of
them have been consumed. The counters are required to
ensure that all items eventually get consumed.
```C++
ConcurrentQueue<Item> q;
const int ProducerCount = 8;
const int ConsumerCount = 8;
std::thread producers[ProducerCount];
std::thread consumers[ConsumerCount];
std::atomic<int> doneProducers(0);
std::atomic<int> doneConsumers(0);
for (int i = 0; i != ProducerCount; ++i) {
	producers[i] = std::thread([&]() {
		while (produce) {
			q.enqueue(produceItem());
		}
		doneProducers.fetch_add(1, std::memory_order_release);
	});
}
for (int i = 0; i != ConsumerCount; ++i) {
	consumers[i] = std::thread([&]() {
		Item item;
		bool itemsLeft;
		do {
			// It's important to fence (if the producers have finished) *before* dequeueing
			itemsLeft = doneProducers.load(std::memory_order_acquire) != ProducerCount;
			while (q.try_dequeue(item)) {
				itemsLeft = true;
				consumeItem(item);
			}
		} while (itemsLeft || doneConsumers.fetch_add(1, std::memory_order_acq_rel) + 1 == ConsumerCount);
		// The condition above is a bit tricky, but it's necessary to ensure that the
		// last consumer sees the memory effects of all the other consumers before it
		// calls try_dequeue for the last time
	});
}
for (int i = 0; i != ProducerCount; ++i) {
	producers[i].join();
}
for (int i = 0; i != ConsumerCount; ++i) {
	consumers[i].join();
}
```
## Producer/consumer model (simultaneous, blocking)

The blocking version is different, since either the number of elements being produced needs
to be known ahead of time, or some other coordination is required to tell the consumers when
to stop calling wait_dequeue (not shown here). This is necessary because otherwise a consumer
could end up blocking forever -- and destroying a queue while a consumer is blocking on it leads
to undefined behaviour.
```C++
BlockingConcurrentQueue<Item> q;
const int ProducerCount = 8;
const int ConsumerCount = 8;
std::thread producers[ProducerCount];
std::thread consumers[ConsumerCount];
std::atomic<int> promisedElementsRemaining(ProducerCount * 1000);
for (int i = 0; i != ProducerCount; ++i) {
	producers[i] = std::thread([&]() {
		for (int j = 0; j != 1000; ++j) {
			q.enqueue(produceItem());
		}
	});
}
for (int i = 0; i != ConsumerCount; ++i) {
	consumers[i] = std::thread([&]() {
		Item item;
		while (promisedElementsRemaining.fetch_sub(1, std::memory_order_relaxed)) {
			q.wait_dequeue(item);
			consumeItem(item);
		}
	});
}
for (int i = 0; i != ProducerCount; ++i) {
	producers[i].join();
}
for (int i = 0; i != ConsumerCount; ++i) {
	consumers[i].join();
}
```

## Producer/consumer model (separate stages)
```C++
ConcurrentQueue<Item> q;

// Production stage
std::thread threads[8];
for (int i = 0; i != 8; ++i) {
	threads[i] = std::thread([&]() {
		while (produce) {
			q.enqueue(produceItem());
		}
	});
}
for (int i = 0; i != 8; ++i) {
	threads[i].join();
}

// Consumption stage
std::atomic<int> doneConsumers(0);
for (int i = 0; i != 8; ++i) {
	threads[i] = std::thread([&]() {
		Item item;
		do {
			while (q.try_dequeue(item)) {
				consumeItem(item);
			}
			// Loop again one last time if we're the last producer (with the acquired
			// memory effects of the other producers):
		} while (doneConsumers.fetch_add(1, std::memory_order_acq_rel) + 1 == 8);
	});
}
for (int i = 0; i != 8; ++i) {
	threads[i].join();
}
```
Note that there's no point trying to use the blocking queue with this model, since
there's no need to use the `wait` methods (all the elements are produced before any
are consumed), and hence the complexity would be the same but with additional overhead.


## Object pool

If you don't know what threads will be using the queue in advance,
you can't really declare any long-term tokens. The obvious solution
is to use the implicit methods (that don't take any tokens):
```C++
// A pool of 'Something' objects that can be safely accessed
// from any thread
class SomethingPool
{
public:
    Something getSomething()
    {
	Something obj;
	queue.try_dequeue(obj);

	// If the dequeue succeeded, obj will be an object from the
	// thread pool, otherwise it will be the default-constructed
	// object as declared above
	return obj;
    }

    void recycleSomething(Something&& obj)
    {
	queue.enqueue(std::move(obj));
    }
};
```

## Threadpool task queue
```C++
BlockingConcurrentQueue<Task> q;

// To create a task from any thread:
q.enqueue(...);

// On threadpool threads:
Task task;
while (true) {
	q.wait_dequeue(task);

	// Process task...
}
```

## Multithreaded game loop
```C++
BlockingConcurrentQueue<Task> q;
std::atomic<int> pendingTasks(0);

// On threadpool threads:
Task task;
while (true) {
	q.wait_dequeue(task);

	// Process task...

	pendingTasks.fetch_add(-1, std::memory_order_release);
}

// Whenever a new task needs to be processed for the frame:
pendingTasks.fetch_add(1, std::memory_order_release);
q.enqueue(...);

// To wait for all the frame's tasks to complete before rendering:
while (pendingTasks.load(std::memory_order_acquire) != 0)
	continue;

// Alternatively you could help out the thread pool while waiting:
while (pendingTasks.load(std::memory_order_acquire) != 0) {
	if (!q.try_dequeue(task)) {
		continue;
	}

	// Process task...

	pendingTasks.fetch_add(-1, std::memory_order_release);
}
```

## Pump until empty

This might be useful if, for example, you want to process any remaining items
in the queue before it's destroyed. Note that it is your responsibility
to ensure that the memory effects of any enqueue operations you wish to see on
the dequeue thread are visible (i.e. if you're waiting for a certain set of elements,
you need to use memory fences to ensure that those elements are visible to the dequeue
thread after they've been enqueued).
```C++
ConcurrentQueue<Item> q;

// Single-threaded pumping:
Item item;
while (q.try_dequeue(item)) {
	// Process item...
}
// q is guaranteed to be empty here, unless there is another thread enqueueing still or
// there was another thread dequeueing at one point and its memory effects have not
// yet been propagated to this thread.

// Multi-threaded pumping:
std::thread threads[8];
std::atomic<int> doneConsumers(0);
for (int i = 0; i != 8; ++i) {
	threads[i] = std::thread([&]() {
		Item item;
		do {
			while (q.try_dequeue(item)) {
				// Process item...
			}
		} while (doneConsumers.fetch_add(1, std::memory_order_acq_rel) + 1 == 8);
		// If there are still enqueue operations happening on other threads,
		// then the queue may not be empty at this point. However, if all enqueue
		// operations completed before we finished pumping (and the propagation of
		// their memory effects too), and all dequeue operations apart from those
		// our threads did above completed before we finished pumping (and the
		// propagation of their memory effects too), then the queue is guaranteed
		// to be empty at this point.
	});
}
for (int i = 0; i != 8; ++i) {
	threads[i].join();
}
```

## Wait for a queue to become empty (without dequeueing)

You can't (robustly) :-) However, you can set up your own atomic counter and
poll that instead (see the game loop example). If you're satisfied with merely an estimate, you can use
`size_approx()`. Note that `size_approx()` may return 0 even if the queue is
not completely empty, unless the queue has already stabilized first (no threads
are enqueueing or dequeueing, and all memory effects of any previous operations
have been propagated to the thread before it calls `size_approx()`).
