# moodycamel::ConcurrentQueue<T>

An industrial-strength lock-free queue for C++.

Note: If all you need is a single-producer, single-consumer queue, I have [one of those too][spsc].

## Features

- Knock-your-socks-off [blazing fast performance][benchmarks].
- Single-header implementation. Just drop it in your project.
- General-purpose lock-free queue. Can be used concurrently from any number of threads.
- C++11 implementation -- elements are moved (instead of copied) where possible.
- Templated, obviating the need to deal exclusively with pointers -- memory is managed for you.
- No limitations in terms of the type or quantity of elements that can be put in the queue.
- Supports use as a fixed-size queue (memory only allocated once up-front).
- Supports use as a dynamically-sized queue (some memory is allocated up-front, then more as needed).
- Supports super-fast bulk operations.

## Reasons to use

There are not that many full-fledged lock-free queues for C++. Boost has one, but it's limited to objects with trivial
assignment operators and trivial destructors, for example. Intel's TBB queue isn't lock-free, and requires trivial constructors too.
There's many academic papers that implement lock-free queues in C++, but usable source code is
hard to find, and tests even more so.

This queue not only has less limitations than others (for the most part), but [it's also faster][benchmarks].
It's been fairly well-tested, and offers advanced features like **bulk enqueueing/dequeueing**
(which, with my new design, is much faster than one element at a time, approaching and even surpassing
the speed of a non-concurrent queue even under heavy contention).

In short, there was a lock-free queue shaped hole in the C++ open-source universe, and I set out
to fill it with the fastest, most complete, and well-tested design and implementation I could.
The result is `moodycamel::ConcurrentQueue` :-)

## Reasons *not* to use

The fastest synchronization of all is the kind that never takes place. Fundamentally,
concurrent data structures require some synchronization, and that takes time. Every effort
was made, of course, to minimize the overhead, but if you can avoid sharing data between
threads, do so!

Why use concurrent data structures at all, then? Because they're gosh darn convenient! (And, indeed,
sometimes sharing data concurrently is unavoidable.)

My queue is **not linearizable** (see the next section on high-level design). The foundations of
its design assume that producers are independent; if this is not the case, and your producers
co-ordinate amongst themselves in some fashion, be aware that the elements won't necessarily
come of the queue in the same order they were put in *relative to the ordering formed by that co-ordination*
(but they will still come out in the order they were put in by any *individual* producer). If this affects
your use case, you may be better off with another implementation; either way, it's an important limitation
to be aware of.

Note also that the implementation is (presently) **not exception safe**, so if you use exceptions, make sure that
they can't be thrown from within the constructor and assignment operator of your object type. The
queue itself never throws any exceptions (even for memory allocation failures, which it handles gracefully).

My queue is also **not NUMA aware**, and does a lot of memory re-use internally, meaning it probably doesn't
scale particularly well on NUMA architectures; however, I don't know of any other lock-free queue that *is*
NUMA aware (except for [SALSA][salsa], which is very cool, but has no publicly available implementation that I know of).

## High-level design

Elements are stored internally using contiguous blocks instead of linked lists for better performance.
The queue is made up of a collection of sub-queues, one for each producer thread. When a
consumer wants to dequeue an element, it checks all the sub-queues until it finds one.
All of this is largely transparent to the user of the queue, however -- it just works<sup>TM</sup>.

One particular consequence of this design (which seems to be non-intuitive) is that if two producers
enqueue at the same time, there is no defined ordering between the elements when they're later dequeued.
Normally this is fine, because even with a fully linearizable queue there'd be a race between the producer
threads and so you couldn't rely on the ordering anyway. However, if for some reason you do extra explicit synchronization
between the two producer threads yourself, thus defining a total order between enqueue operations, you might expect
that the elements would come out in the same total order, which is a guarantee my queue does not offer. At that
point, though, there semantically aren't really two separate producers, but rather one that happens to be spread
across two threads. In this case, you can still establish a total ordering with my queue by creating
a single producer token, and using that from both threads to enqueue (taking care to synchronize access to the token,
of course, but there was already extra synchronization involved anyway).

I've written a more detailed [overview of the internal design][blog], as well as [the full
nitty-gritty details of the design][design], on my blog. Finally, the
[source][source] itself is available for perusal for those interested in its implementation.

## Basic use

The entire queue's implementation is contained in **one header**, [`concurrentqueue.h`][concurrentqueue.h].
Simply download and include that to use the queue.
The implementation makes use of certain key C++11 features, so it requires a fairly recent compiler
(e.g. g++ 4.8; note that g++ 4.6 has a known bug with `std::atomic` and is thus not supported).
The code itself is platform independent.

Use it like you would any other templated queue, with the exception that you can use
it from many threads at once :-)

Simple example:

    #include "concurrentqueue.h"
    
    moodycamel::ConcurrentQueue<int> q;
    q.enqueue(25);
    
    int item;
    bool found = q.try_dequeue(item);
    assert(found && item == 25);

Description of basic methods:
- `ConcurrentQueue(size_t initialSizeEstimate)`
      Constructor which optionally accepts an estimate of the number of elements the queue will hold
- `enqueue(T&& item)`
      Enqueues one item, allocating extra space if necessary
- `try_enqueue(T&& item)`
      Enqueues one item, but only if enough memory is already allocated
- `try_dequeue(T& item)`
      Dequeues one item, returning true if an item was found or false if the queue appeared empty

Note that it is up to the user to ensure that the object is completely constructed before
being used by any other threads (this includes making the memory effects of construction
visible, possibly via a memory barrier). Similarly, it's important that all threads have
finished using the queue (and the memory effects have fully propagated) before it is
destructed.

Full API (pseudocode):

	# Allocates more memory if necessary
	enqueue(item) : bool
	enqueue(prod_token, item) : bool
	enqueue_bulk(item_first, count) : bool
	enqueue_bulk(prod_token, item_first, count) : bool
	
	# Fails if not enough memory to enqueue
	try_enqueue(item) : bool
	try_enqueue(prod_token, item) : bool
	try_enqueue_bulk(item_first, count) : bool
	try_enqueue_bulk(prod_token, item_first, count) : bool
	
	# Attempts to dequeue from the queue (never allocates)
	try_dequeue(item&) : bool
	try_dequeue(cons_token, item&) : bool
	try_dequeue_bulk(item_first, max) : size_t
	try_dequeue_bulk(cons_token, item_first, max) : size_t
	
	# If you happen to know which producer you want to dequeue from
	try_dequeue_from_producer(prod_token, item&) : bool
	try_dequeue_bulk_from_producer(prod_token, item_first, max) : size_t
	
	# A not-necessarily-accurate count of the total number of elements
	size_approx() : size_t

## Advanced features

#### Bulk operations

Thanks to the [novel design][blog] of the queue, it's just as easy to enqueue/dequeue multiple
items as it is to do one at a time. This means that overhead can be cut drastically for
bulk operations. Example syntax:

    moodycamel::ConcurrentQueue<int> q;

	int items[] = { 1, 2, 3, 4, 5 };
    q.enqueue_bulk(items, 5);
    
    int results[5];		// Could also be any iterator
    size_t count = q.try_dequeue_bulk(results, 5);
    for (size_t i = 0; i != count; ++i) {
    	assert(results[i] == items[i]);
    }
    
#### Task-specific tokens

Additionally, the queue can take advantage of having thread-local data if
it's available. This takes the form of "tokens": you can create a consumer
token and/or a producer token for each thread or task, and use the methods
that accept a token as their first parameter:

    moodycamel::ConcurrentQueue<int> q;
    
    moodycamel::ProducerToken ptok(q);
    q.enqueue(ptok, 17);
    
    moodycamel::ConsumerToken ctok(q);
    int item;
    q.try_dequeue(ctok, item);
    assert(item == 17);

If you happen to know which producer you want to consume from (e.g. in
a single-producer, multi-consumer scenario), you can use the `try_dequeue_from_producer`
methods, which accept a producer token instead of a consumer token, and cut some overhead.

When producing or consuming many elements, the most efficient way is to:
1. Use the bulk methods of the queue with tokens
2. Failing that, use the bulk methods without tokens
3. Failing that, use the single-item methods with tokens
4. Failing that, use the single-item methods without tokens

Having said that, don't create tokens willy-nilly -- ideally there should be
a maximum of one token (of each kind) per thread. The queue will do its best
with what it is given, but it performs best when passed tokens.

#### Traits

The queue also supports a traits template argument which defines various types, constants,
and the memory allocation and deallocation functions that are to be used by the queue. The typical pattern
to providing your own traits is to create a class that inherits from the default traits
and override only the values you wish to change. Example:

    struct MyTraits : public moodycamel::ConcurrentQueueDefaultTraits
    {
    	static const size_t BLOCK_SIZE = 256;		// Use bigger blocks
    };
    
    moodycamel::ConcurrentQueue<int, MyTraits> q;

#### How to dequeue types without calling the constructor

The normal way to dequeue an item is to pass in an existing object by reference, which
is then assigned to internally by the queue (using the move-assignment operator if possible).
This can pose a problem for types that are
expensive to construct or don't have a default constructor; fortunately, there is a simple
workaround: Create a wrapper class that copies the memory contents of the object when it
is assigned by the queue (a poor man's move, essentially). Note that this only works if
the object contains no internal pointers. Example:

    struct MyObjectMover
    {
        inline void operator=(MyObject&& obj)
        {
            std::memcpy(data, &obj, sizeof(MyObject));
            
            // TODO: Cleanup obj so that when it's destructed by the queue
            // it doesn't corrupt the data of the object we just moved it into
        }
        
        inline MyObject& obj() { return *reinterpret_cast<MyObject*>(data); }
    	
    private:
    	align(alignof(MyObject)) char data[sizeof(MyObject)];
    };

## Samples

There are some more detailed samples [here][samples.md]. The source of
the [unit tests][unittest-src] and [benchmarks][benchmark-src] are available for reference as well.

## Benchmarks

See my blog post for some [benchmark results][benchmarks] (including versus `boost::lockfree::queue` and `tbb::concurrent_queue`),
or run the benchmarks yourself (requires MinGW and certain GnuWin32 utilities to build on Windows, or a recent
g++ on Linux):

    cd build
    make benchmarks
    bin/benchmarks

The short version of the benchmarks is that it's so fast (especially the bulk methods), that if you're actually
using the queue to *do* anything, the queue won't be your bottleneck.

## Tests (and bugs)

I've written quite a few unit tests as well as a randomized long-running fuzz tester. I also ran the
core queue algorithm through the [CDSChecker][cdschecker] C++11 memory model model checker. Some of the
inner algorithms were tested using the [Relacy][relacy] model checker.
I've tested
on Linux (Fedora 19) and Windows (7), but only on x86 processors so far (Intel and AMD). The code was
written to be platform-independent, however, and should work across all processors.

Due to the complexity of the implementation and the difficult-to-test nature of lock-free code in general,
there may still be bugs. If anyone is seeing buggy behaviour, I'd like to hear about it! (Especially if
a unit test for it can be cooked up.) Just open an issue on GitHub.


## License

I'm releasing the source of this repository (with the exception of third-party code, i.e. the Boost queue
(used in the benchmarks for comparison), Intel's TBB library (ditto), CDSChecker, and Relacy, which have their own licenses) under a [simplified BSD license][license].

Note that lock-free programming is a patent minefield, and this code may very
well violate a pending patent (I haven't looked), though it does not to my present knowledge.
I did design and implement this queue from scratch.


[blog]: http://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++
[design]: http://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue
[samples.md]: https://github.com/cameron314/concurrentqueue/blob/master/samples.md
[source]: https://github.com/cameron314/concurrentqueue
[concurrentqueue.h]: https://github.com/cameron314/concurrentqueue/blob/master/concurrentqueue.h
[unittest-src]: https://github.com/cameron314/concurrentqueue/tree/master/tests/unittests
[benchmarks]: http://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++#benchmarks
[benchmark-src]: https://github.com/cameron314/concurrentqueue/tree/master/benchmarks
[license]: https://github.com/cameron314/concurrentqueue/blob/master/LICENSE.md
[cdschecker]: http://demsky.eecs.uci.edu/c11modelchecker.html
[relacy]: http://www.1024cores.net/home/relacy-race-detector
[spsc]: https://github.com/cameron314/readerwriterqueue
[salsa]: http://webee.technion.ac.il/~idish/ftp/spaa049-gidron.pdf
