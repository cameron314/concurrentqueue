#pragma once

//#define MCDBGQ_TRACKMEM 1
//#define MCDBGQ_NOLOCKFREE_FREELIST 1
//#define MCDBGQ_USEDEBUGFREELIST 1
//#define MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX 1
//#define MCDBGQ_NOLOCKFREE_IMPLICITPRODHASH 1

#if defined(_WIN32) || defined(__WINDOWS__) || defined(__WIN32__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace moodycamel { namespace debug {
	struct DebugMutex {
		DebugMutex() { InitializeCriticalSectionAndSpinCount(&cs, 0x400); }
		~DebugMutex() { DeleteCriticalSection(&cs); }
		
		void lock() { EnterCriticalSection(&cs); }
		void unlock() { LeaveCriticalSection(&cs); }
		
	private:
		CRITICAL_SECTION cs;
	};
} }
#else
#include <mutex>
namespace moodycamel { namespace debug {
	struct DebugMutex {
		void lock() { m.lock(); }
		void unlock() { m.unlock(); }
		
	private:
		std::mutex m;
	};
} }
#define
#endif

namespace moodycamel { namespace debug {
	struct DebugLock {
		explicit DebugLock(DebugMutex& mutex)
			: mutex(mutex)
		{
			mutex.lock();
		}
		
		~DebugLock()
		{
			mutex.unlock();
		}
		
	private:
		DebugMutex& mutex;
	};
	
	
	template<typename N>
	struct DebugFreeList {
		DebugFreeList() : head(nullptr) { }
		DebugFreeList(DebugFreeList&& other) : head(other.head) { other.head = nullptr; }
		void swap(DebugFreeList& other) { std::swap(head, other.head); }
		
		inline void add(N* node)
		{
			DebugLock lock(mutex);
			node->freeListNext = head;
			head = node;
		}
		
		inline N* try_get()
		{
			DebugLock lock(mutex);
			if (head == nullptr) {
				return nullptr;
			}
			
			auto prevHead = head;
			head = head->freeListNext;
			return prevHead;
		}
		
		N* head_unsafe() const { return head; }
		
	private:
		N* head;
		DebugMutex mutex;
	};
} }
