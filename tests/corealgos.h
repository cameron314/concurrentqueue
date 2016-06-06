// ©2014 Cameron Desrochers

// moodycamel::ConcurrentQueue contains many inner data structures which
// are difficult to test in isolation. So, this file contains copies of
// them, extracted and isolated so as to be independently testable.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <utility>
#include <limits>
#include <cassert>


// Define corealgos_allocator before including this header in order to override the
// default malloc/free functions
#ifndef corealgos_allocator
struct corealgos_allocator
{
	static inline void* malloc(std::size_t size) { return std::malloc(size); }
	static inline void free(void* ptr) { std::free(ptr); }
};
#endif


////////////////////////////////////////////////////////////////////////////////
// Lock-free add-only list (e.g. used to track producers)
////////////////////////////////////////////////////////////////////////////////

namespace moodycamel { namespace corealgos {

struct ListItem
{
	ListItem()
		: concurrentListPrev(nullptr)
	{
	}
	
public:
	std::atomic<ListItem*> concurrentListPrev;
};

template<typename T>		// T should inherit ListItem or implement the same interface
struct ConcurrentAddOnlyList
{
	ConcurrentAddOnlyList()
		: tail_(nullptr)
	{
	}
	
	inline T* tail() { return tail_.load(std::memory_order_acquire); }
	
	void add(T* element)
	{
		assert(element != nullptr);
		
		// Add it to the lock-free list
		auto prevTail = tail_.load(std::memory_order_relaxed);
		do {
			element->concurrentListPrev = prevTail;
		} while (!tail_.compare_exchange_weak(prevTail, element, std::memory_order_release, std::memory_order_relaxed));
	}
	
private:
	std::atomic<T*> tail_;
};

} }



////////////////////////////////////////////////////////////////////////////////
// Thread local hash map
////////////////////////////////////////////////////////////////////////////////

#if defined(__APPLE__)
#include "TargetConditionals.h" // Needed for TARGET_OS_IPHONE
#endif

// Platform-specific definitions of a numeric thread ID type and an invalid value
#if defined(_WIN32) || defined(__WINDOWS__) || defined(__WIN32__)
// No sense pulling in windows.h in a header, we'll manually declare the function
// we use and rely on backwards-compatibility for this not to break
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
namespace moodycamel { namespace corealgos { namespace details {
	static_assert(sizeof(unsigned long) == sizeof(std::uint32_t), "Expected size of unsigned long to be 32 bits on Windows");
	typedef std::uint32_t thread_id_t;
	static const thread_id_t invalid_thread_id = 0;		// See http://blogs.msdn.com/b/oldnewthing/archive/2004/02/23/78395.aspx
	static inline thread_id_t thread_id() { return static_cast<thread_id_t>(::GetCurrentThreadId()); }
} } }
#elif defined(__arm__) || defined(_M_ARM) || defined(__aarch64__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
namespace moodycamel { namespace corealgos { namespace details {
	typedef std::uintptr_t thread_id_t;
	static const thread_id_t invalid_thread_id = 0;
	static inline thread_id_t thread_id() { return std::hash<std::thread::id>()(std::this_thread::get_id()); }
} } }
#else
// Use a nice trick from this answer: http://stackoverflow.com/a/8438730/21475
// In order to get a numeric thread ID in a platform-independent way, we use a thread-local
// static variable's address as a thread identifier :-)
#if defined(__GNUC__) || defined(__INTEL_COMPILER)
#define MOODYCAMEL_COREALGO_THREADLOCAL __thread
#elif defined(_MSC_VER)
#define MOODYCAMEL_COREALGO_THREADLOCAL __declspec(thread)
#else
// Assume C++11 compliant compiler
#define MOODYCAMEL_COREALGO_THREADLOCAL thread_local
#endif
namespace moodycamel { namespace corealgos { namespace details {
	typedef std::uintptr_t thread_id_t;
	static const thread_id_t invalid_thread_id = 0;		// Address can't be nullptr
	static inline thread_id_t thread_id() { static MOODYCAMEL_COREALGO_THREADLOCAL int x; return reinterpret_cast<thread_id_t>(&x); }
} } }
#endif

namespace moodycamel { namespace corealgos {

namespace details
{
	template<bool use32> struct _hash_32_or_64 {
		static inline std::size_t hash(std::uint32_t h)
		{
			// MurmurHash3 finalizer -- see https://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp
			// Since the thread ID is already unique, all we really want to do is propagate that
			// uniqueness evenly across all the bits, so that we can use a subset of the bits while
			// reducing collisions significantly
			h ^= h >> 16;
			h *= 0x85ebca6b;
			h ^= h >> 13;
			h *= 0xc2b2ae35;
			return static_cast<std::size_t>(h ^ (h >> 16));
		}
	};
	template<> struct _hash_32_or_64<1> {
		static inline std::size_t hash(std::uint64_t h)
		{
			h ^= h >> 33;
			h *= 0xff51afd7ed558ccd;
			h ^= h >> 33;
			h *= 0xc4ceb9fe1a85ec53;
			return static_cast<std::size_t>(h ^ (h >> 33));
		}
	};
	template<std::size_t size> struct hash_32_or_64 : public _hash_32_or_64<(size > 4)> {  };
	
	static inline std::size_t hash_thread_id(thread_id_t id)
	{
		static_assert(sizeof(thread_id_t) <= 8, "Expected a platform where thread IDs are at most 64-bit values");
		return hash_32_or_64<sizeof(thread_id_t)>::hash(id);
	}
	
	template<typename U>
	static inline char* align_for(char* ptr)
	{
		const std::size_t alignment = std::alignment_of<U>::value;
		return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
	}
}


template<typename T>		// T should inherit ListItem or implement the same interface
struct ThreadLocal
{
	explicit ThreadLocal(std::size_t initialHashSize)
		: initialHashEntries(initialHashSize)
	{
		assert(initialHashSize > 0 && (initialHashSize & (initialHashSize - 1)) == 0);
		
		resizeInProgress.clear();
		currentHashCount.store(0, std::memory_order_relaxed);
		auto hash = &initialHash;
		hash->capacity = initialHashSize;
		hash->entries = &initialHashEntries[0];
		for (std::size_t i = 0; i != initialHashSize; ++i) {
			initialHashEntries[i].key.store(details::invalid_thread_id, std::memory_order_relaxed);
		}
		hash->prev = nullptr;
		currentHash.store(hash, std::memory_order_relaxed);
	}
	
	~ThreadLocal()
	{
		// Destroy items
		auto ptr = items.tail();
		while (ptr != nullptr) {
			auto prev = static_cast<T*>(ptr->concurrentListPrev.load(std::memory_order_relaxed));
			ptr->~T();
			corealgos_allocator::free(ptr);
			ptr = prev;
		}
		
		// Destroy hash tables
		auto hash = currentHash.load(std::memory_order_relaxed);
		while (hash != nullptr) {
			auto prev = hash->prev;
			if (prev != nullptr) {		// The last hash is part of this object and was not allocated dynamically
				for (std::size_t i = 0; i != hash->capacity; ++i) {
	                hash->entries[i].~KeyValuePair();
	            }
	            hash->~InnerHash();
				corealgos_allocator::free(hash);
			}
			hash = prev;
		}
	}
	
	// Only fails (returns nullptr) if memory allocation fails
	T* get_or_create()
	{
		// Note that since the data is essentially thread-local (key is thread ID),
		// there's a reduced need for fences (memory ordering is already consistent
		// for any individual thread), except for the current table itself
		
		// Start by looking for the thread ID in the current and all previous hash tables.
		// If it's not found, it must not be in there yet, since this same thread would
		// have added it previously to one of the tables that we traversed.
		
		// Code and algorithm adapted from http://preshing.com/20130605/the-worlds-simplest-lock-free-hash-table
		
		auto id = details::thread_id();
		auto hashedId = details::hash_thread_id(id);
		
		auto mainHash = currentHash.load(std::memory_order_acquire);
		for (auto hash = mainHash; hash != nullptr; hash = hash->prev) {
			// Look for the id in this hash
			auto index = hashedId;
			while (true) {		// Not an infinite loop because at least one slot is free in the hash table
				index &= hash->capacity - 1;
				
				auto probedKey = hash->entries[index].key.load(std::memory_order_relaxed);
				if (probedKey == id) {
					// Found it! If we had to search several hashes deep, though, we should lazily add it
					// to the current main hash table to avoid the extended search next time.
					// Note there's guaranteed to be room in the current hash table since every subsequent
					// table implicitly reserves space for all previous tables (there's only one
					// currentHashCount).
					auto value = hash->entries[index].value;
					if (hash != mainHash) {
						index = hashedId;
						while (true) {
							index &= mainHash->capacity - 1;
							probedKey = mainHash->entries[index].key.load(std::memory_order_relaxed);
							auto expected = details::invalid_thread_id;
							if (probedKey == expected && mainHash->entries[index].key.compare_exchange_strong(expected, id, std::memory_order_relaxed)) {
								mainHash->entries[index].value = value;
								break;
							}
							++index;
						}
					}
					
					return value;
				}
				if (probedKey == details::invalid_thread_id) {
					break;		// Not in this hash table
				}
		        ++index;
		    }
		}
		
		// Insert!
		auto newCount = 1 + currentHashCount.fetch_add(1, std::memory_order_relaxed);
		while (true) {
			if (newCount >= (mainHash->capacity >> 1) && !resizeInProgress.test_and_set(std::memory_order_acquire)) {
				// We've acquired the resize lock, try to allocate a bigger hash table.
				// Note the acquire fence synchronizes with the release fence at the end of this block, and hence when
				// we reload currentHash it must be the most recent version (it only gets changed within this
				// locked block).
				mainHash = currentHash.load(std::memory_order_acquire);
				auto newCapacity = mainHash->capacity << 1;
				while (newCount >= (newCapacity >> 1)) {
					newCapacity <<= 1;
				}
				auto raw = static_cast<char*>(corealgos_allocator::malloc(sizeof(InnerHash) + std::alignment_of<KeyValuePair>::value - 1 + sizeof(KeyValuePair) * newCapacity));
				if (raw == nullptr) {
					// Allocation failed
					currentHashCount.fetch_add(-1, std::memory_order_relaxed);
					resizeInProgress.clear(std::memory_order_relaxed);
					return nullptr;
				}
				
				auto newHash = new (raw) InnerHash;
				newHash->capacity = newCapacity;
				newHash->entries = reinterpret_cast<KeyValuePair*>(details::align_for<KeyValuePair>(raw + sizeof(InnerHash)));
				for (std::size_t i = 0; i != newCapacity; ++i) {
					new (newHash->entries + i) KeyValuePair;
					newHash->entries[i].key.store(details::invalid_thread_id, std::memory_order_relaxed);
				}
				newHash->prev = mainHash;
				currentHash.store(newHash, std::memory_order_release);
				resizeInProgress.clear(std::memory_order_release);
				mainHash = newHash;
			}
			
			// If it's < three-quarters full, add to the old one anyway so that we don't have to wait for the next table
			// to finish being allocated by another thread (and if we just finished allocating above, the condition will
			// always be true)
			if (newCount < (mainHash->capacity >> 1) + (mainHash->capacity >> 2)) {
				auto element = (T*)corealgos_allocator::malloc(sizeof(T));
				if (element == nullptr) {
					return nullptr;
				}
				new (element) T();
				items.add(element);		// Track items so they can be destructed later
				
				auto index = hashedId;
				while (true) {
					index &= mainHash->capacity - 1;
					auto probedKey = mainHash->entries[index].key.load(std::memory_order_relaxed);
					auto expected = details::invalid_thread_id;
					if (probedKey == expected && mainHash->entries[index].key.compare_exchange_strong(expected, id, std::memory_order_relaxed)) {
						mainHash->entries[index].value = element;
						break;
					}
					++index;
				}
				return element;
			}
			
			// Hmm, the old hash is quite full and somebody else is busy allocating a new one.
			// We need to wait for the allocating thread to finish (if it succeeds, we add, if not,
			// we try to allocate ourselves).
			mainHash = currentHash.load(std::memory_order_acquire);
		}
	}
	
private:
	struct KeyValuePair
	{
		std::atomic<details::thread_id_t> key;
		T* value;		// No need for atomicity since it's only read by the thread that sets it in the first place

		KeyValuePair()
		{ }

		KeyValuePair(KeyValuePair const& other)
			: key(other.key.load()), value(other.value)
		{ }

		KeyValuePair& operator=(KeyValuePair const& other)
		{
			key.store(other.key.load());
			value = other.value;
			return *this;
		}
	};
	
	struct InnerHash
	{
		std::size_t capacity;
		KeyValuePair* entries;
		InnerHash* prev;
	};
	
	std::atomic_flag resizeInProgress;
	std::atomic<InnerHash*> currentHash;
	std::atomic<std::size_t> currentHashCount;		// Number of slots logically used
	InnerHash initialHash;
	std::vector<KeyValuePair> initialHashEntries;
	ConcurrentAddOnlyList<T> items;
};





////////////////////////////////////////////////////////////////////////////////
// Lock-free free list
////////////////////////////////////////////////////////////////////////////////

template <typename N>
struct FreeListNode
{
    FreeListNode() : freeListRefs(0), freeListNext(nullptr) { }

    std::atomic<std::uint32_t> freeListRefs;
    std::atomic<N*> freeListNext;

	FreeListNode(FreeListNode const& other)
		: freeListRefs(other.freeListRefs.load()), freeListNext(other.freeListNext.load())
	{ }

	FreeListNode& operator=(FreeListNode const& other)
	{
		freeListRefs.store(other.freeListRefs.load());
		freeListNext.store(other.freeListNext.load());
		return *this;
	}
};

// A simple CAS-based lock-free free list. Not the fastest thing in the world under heavy contention,
// but simple and correct (assuming nodes are never freed until after the free list is destroyed),
// and fairly speedy under low contention.
template<typename N>    // N must inherit FreeListNode or have the same fields (and initialization)
struct FreeList
{
    FreeList() : freeListHead(nullptr) { }

    inline void add(N* node)
    {
        // We know that the should-be-on-freelist bit is 0 at this point, so it's safe to
        // set it using a fetch_add
        if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST, std::memory_order_acq_rel) == 0) {
            // Oh look! We were the last ones referencing this node, and we know
            // we want to add it to the free list, so let's do it!
     	    add_knowing_refcount_is_zero(node);
        }
    }

    inline N* try_get()
    {
        auto head = freeListHead.load(std::memory_order_acquire);
        while (head != nullptr) {
            auto prevHead = head;
            auto refs = head->freeListRefs.load(std::memory_order_relaxed);
            if ((refs & REFS_MASK) == 0 || !head->freeListRefs.compare_exchange_strong(refs, refs + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                head = freeListHead.load(std::memory_order_acquire);
                continue;
            }

            // Good, reference count has been incremented (it wasn't at zero), which means
            // we can read the next and not worry about it changing between now and the time
            // we do the CAS
            auto next = head->freeListNext.load(std::memory_order_relaxed);
            if (freeListHead.compare_exchange_strong(head, next,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                // Yay, got the node. This means it was on the list, which means
                // shouldBeOnFreeList must be false no matter the refcount (because
                // nobody else knows it's been taken off yet, it can't have been put back on).
           		assert((head->freeListRefs.load(std::memory_order_relaxed) & SHOULD_BE_ON_FREELIST) == 0);

                // Decrease refcount twice, once for our ref, and once for the list's ref
                head->freeListRefs.fetch_add(-2, std::memory_order_release);
                return head;
            }

            // OK, the head must have changed on us, but we still need to decrease the refcount we
            // increased.
            // Note that we don't need to release any memory effects, but we do need to ensure that the reference
			// count decrement happens-after the CAS on the head.
            refs = prevHead->freeListRefs.fetch_add(-1, std::memory_order_acq_rel);
            if (refs == SHOULD_BE_ON_FREELIST + 1) {
                add_knowing_refcount_is_zero(prevHead);
            }
        }

        return nullptr;
    }

    // Useful for traversing the list when there's no contention (e.g. to destroy remaining nodes)
    N* head_unsafe() const { return freeListHead.load(std::memory_order_relaxed); }

private:
    inline void add_knowing_refcount_is_zero(N* node)
    {
        // Since the refcount is zero, and nobody can increase it once it's zero (except us, and we
        // run only one copy of this method per node at a time, i.e. the single thread case), then we
        // know we can safely change the next pointer of the node; however, once the refcount is back
        // above zero, then other threads could increase it (happens under heavy contention, when the
        // refcount goes to zero in between a load and a refcount increment of a node in try_get, then
        // back up to something non-zero, then the refcount increment is done by the other thread) --
        // so, if the CAS to add the node to the actual list fails, decrease the refcount and leave
        // the add operation to the next thread who puts the refcount back at zero (which could be us,
        // hence the loop).
        auto head = freeListHead.load(std::memory_order_relaxed);
        while (true) {
            node->freeListNext.store(head, std::memory_order_relaxed);
            node->freeListRefs.store(1, std::memory_order_release);
            if (!freeListHead.compare_exchange_strong(head, node,
                    std::memory_order_release, std::memory_order_relaxed)) {
                // Hmm, the add failed, but we can only try again when the refcount goes back to zero
                if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST - 1, std::memory_order_release) == 1) {
                    continue;
                }
            }
            return;
        }
    }

private:
    static const std::uint32_t REFS_MASK = 0x7FFFFFFF;
    static const std::uint32_t SHOULD_BE_ON_FREELIST = 0x80000000;
	
    // Implemented like a stack, but where node order doesn't matter (nodes are
    // inserted out of order under contention)
    std::atomic<N*> freeListHead;
};



////////////////////////////////////////////////////////////////////////////////
// Lock-free (single-producer, multi-consumer) numeric-key hash map of sorts;
// there are many conditions that must be met, i.e. items have to be inserted
// in increasing order by key (wrap-around is OK), and items cannot be searched
// for or removed unless they are known to be in the map in the first place.
////////////////////////////////////////////////////////////////////////////////

template<typename TValue>
struct SPMCSequentialHashMap
{
	explicit SPMCSequentialHashMap(std::size_t initialSize)
		: nextCapacity(initialSize), index(nullptr)
	{
		new_index();
	}
	
	~SPMCSequentialHashMap()
	{
		auto ptr = index.load(std::memory_order_relaxed);
		if (ptr != nullptr) {
			for (std::size_t i = 0; i != ptr->capacity; ++i) {
				ptr->index[i]->~IndexEntry();
			}
			do {
				auto prev = ptr->prev;
				ptr->~IndexHeader();
				corealgos_allocator::free(ptr);
				ptr = prev;
			} while (ptr != nullptr);
		}
	}
	
	// Not thread safe. Only call from single producer thread.
	// Note: key must *not* be in hash already, and must be exactly
	// one larger than the previously inserted key value.
	void insert(std::uint64_t key, TValue* value)
	{
		IndexEntry* idxEntry;
		insert_index_entry(idxEntry, key);
		idxEntry->value.store(value, std::memory_order_release);
	}
	
	// Thread-safe, but if somebody can remove the key while find() is
	// in progress, then any returned value is not guaranteed to correspond
	// to that key. This also applies if the key was not already present but
	// once was. Elements can be found in any order.
	TValue* find(std::uint64_t key)
	{
		auto idxEntry = get_entry_for_key(key);
		if (idxEntry == nullptr)
			return nullptr;
		return idxEntry->value.load(std::memory_order_acquire);
	}
	
	// Thread-safe, but if somebody else can remove the same key while remove()
	// is in progress, then any removed value is not guaranteed to correspond
	// to that key This also applies if the key was not already present but
	// once was. Elements can be removed in an order.
	TValue* remove(std::uint64_t key)
	{
		auto idxEntry = get_entry_for_key(key);
		if (idxEntry == nullptr)
			return nullptr;
		TValue* val = nullptr;
		while (!idxEntry->value.compare_exchange_weak(val, nullptr, std::memory_order_acquire, std::memory_order_relaxed))
			continue;
		return val;
	}
	
private:
	struct IndexEntry
	{
		std::atomic<std::uint64_t> key;
		std::atomic<TValue*> value;
	};
	
	struct IndexHeader
	{
		std::size_t capacity;
		std::atomic<std::size_t> tail;
		IndexEntry* entries;
		IndexEntry** index;
		IndexHeader* prev;
	};
	
	inline void insert_index_entry(IndexEntry*& idxEntry, std::uint64_t key)
	{
		auto localIndex = index.load(std::memory_order_relaxed);		// We're the only writer thread, relaxed is OK
		auto newTail = (localIndex->tail.load(std::memory_order_relaxed) + 1) & (localIndex->capacity - 1);
		idxEntry = localIndex->index[newTail];
		if (idxEntry->key.load(std::memory_order_relaxed) == INVALID_KEY ||
			idxEntry->value.load(std::memory_order_relaxed) == nullptr) {
			
			idxEntry->key.store(key, std::memory_order_relaxed);
			localIndex->tail.store(newTail, std::memory_order_release);
			return;
		}
		
		// No room in the old index, try to allocate another one!
		new_index();
		localIndex = index.load(std::memory_order_relaxed);
		newTail = (localIndex->tail.load(std::memory_order_relaxed) + 1) & (localIndex->capacity - 1);
		idxEntry = localIndex->index[newTail];
		assert(idxEntry->key.load(std::memory_order_relaxed) == INVALID_KEY);
		idxEntry->key.store(key, std::memory_order_relaxed);
		localIndex->tail.store(newTail, std::memory_order_release);
	}
	
	inline IndexEntry* get_entry_for_key(std::uint64_t key) const
	{
		auto localIndex = index.load(std::memory_order_acquire);
		auto tail = localIndex->tail.load(std::memory_order_acquire);
		auto tailBase = localIndex->index[tail]->key.load(std::memory_order_relaxed);
		if (tailBase == INVALID_KEY) {
			return nullptr;
		}
		auto offset = static_cast<std::size_t>(key - tailBase);
		std::size_t idx = (tail + offset) & (localIndex->capacity - 1);
		auto entry = localIndex->index[idx];
		return entry->key.load(std::memory_order_relaxed) == key ? entry : nullptr;
	}
	
	bool new_index()
	{
		auto prev = index.load(std::memory_order_relaxed);
		std::size_t prevCapacity = prev == nullptr ? 0 : prev->capacity;
		auto entryCount = prev == nullptr ? nextCapacity : prevCapacity;
		auto raw = static_cast<char*>(corealgos_allocator::malloc(
			sizeof(IndexHeader) +
			std::alignment_of<IndexEntry>::value - 1 + sizeof(IndexEntry) * entryCount +
			std::alignment_of<IndexEntry*>::value - 1 + sizeof(IndexEntry*) * nextCapacity));
		if (raw == nullptr) {
			return false;
		}
		
		auto header = new (raw) IndexHeader;
		auto entries = reinterpret_cast<IndexEntry*>(details::align_for<IndexEntry>(raw + sizeof(IndexHeader)));
		auto idx = reinterpret_cast<IndexEntry**>(details::align_for<IndexEntry*>(reinterpret_cast<char*>(entries) + sizeof(IndexEntry) * entryCount));
		if (prev != nullptr) {
			auto prevTail = prev->tail.load(std::memory_order_relaxed);
			auto prevPos = prevTail;
			std::size_t i = 0;
			do {
				prevPos = (prevPos + 1) & (prev->capacity - 1);
				idx[i++] = prev->index[prevPos];
			} while (prevPos != prevTail);
			assert(i == prevCapacity);
		}
		for (std::size_t i = 0; i != entryCount; ++i) {
			new (entries + i) IndexEntry;
			entries[i].key.store(INVALID_KEY, std::memory_order_relaxed);
			entries[i].value.store(nullptr, std::memory_order_relaxed);
			idx[prevCapacity + i] = entries + i;
		}
		header->prev = prev;
		header->entries = entries;
		header->index = idx;
		header->capacity = nextCapacity;
		header->tail.store((prevCapacity - 1) & (nextCapacity - 1), std::memory_order_relaxed);
		
		index.store(header, std::memory_order_release);
		
		nextCapacity <<= 1;
		
		return true;
	}
		
	private:
		std::size_t nextCapacity;
		std::atomic<IndexHeader*> index;
		
		static const std::uint64_t INVALID_KEY = ~(std::uint64_t)0;
};

} }
