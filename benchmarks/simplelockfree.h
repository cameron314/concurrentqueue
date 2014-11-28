// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

#pragma once

#include "wrappers.h"
#include <atomic>
#include <cstdint>

#if defined(_MSC_VER) && _MSC_VER < 1900
#define alignas(T)
#endif

// Fairly simple, yet correct, implementation of a simple lock-free queue based on linked pointers with CAS
template<typename T>
class SimpleLockFreeQueue
{
public:
	typedef DummyToken producer_token_t;
	typedef DummyToken consumer_token_t;
	
	// Total maximum capacity: 2**39 (half a terabyte's worth -- off-by-one aligned indices)
	static const int UBER_BLOCKS = 256;
	static const int UBER_BLOCK_SIZE = 256;
	static const int ULTRA_BLOCK_SIZE = 256;
	static const int SUPER_BLOCK_SIZE = 256;
	static const int BLOCK_SIZE = 128;
	
private:
	static const uint64_t VERSION_MASK = 0xFFFFFF0000000000ULL;
	static const uint64_t VERSION_INCR = 0x0000010000000000ULL;
	static const uint64_t UBER_BLOCK_IDX_MASK	= 0xFF00000000ULL;
	static const uint64_t UBER_BLOCK_MASK 		= 0x00FF000000ULL;
	static const uint64_t ULTRA_BLOCK_MASK 		= 0x0000FF0000ULL;
	static const uint64_t SUPER_BLOCK_MASK 		= 0x000000FF00ULL;
	static const uint64_t BLOCK_MASK 			= 0x00000000FEULL;
	
	static const uint64_t UBER_BLOCK_IDX_SHIFT	= 32;
	static const uint64_t UBER_BLOCK_SHIFT 		= 24;
	static const uint64_t ULTRA_BLOCK_SHIFT		= 16;
	static const uint64_t SUPER_BLOCK_SHIFT		= 8;
	static const uint64_t BLOCK_SHIFT 			= 1;
	
	typedef std::uint64_t idx_t;
	
public:
	SimpleLockFreeQueue()
		: nextNodeIdx(2), freeListHead(0)
	{
		// Invariants: Head and tail are never null
		auto initialNode = allocate_blank_node();
		head.store(set_consumed_flag(initialNode), std::memory_order_relaxed);
		tail.store(initialNode, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}
	
	~SimpleLockFreeQueue()
	{
		std::atomic_thread_fence(std::memory_order_seq_cst);
		idx_t idx = head.load(std::memory_order_relaxed);
		if (is_consumed(idx)) {
			idx = clear_consumed_flag(idx);
			auto node = get_node_at(idx);
			auto next = node->next.load(std::memory_order_relaxed);
			node->~Node();
			idx = next;
		}
		while (idx != 0) {
			auto node = get_node_at(idx);
			auto next = node->next.load(std::memory_order_relaxed);
			node->item()->~T();
			node->~Node();
			idx = next;
		}
		
		idx = freeListHead.load(std::memory_order_relaxed);
		while (idx != 0) {
			auto node = get_node_at(idx);
			auto next = node->next.load(std::memory_order_relaxed);
			node->~Node();
			idx = next;
		}
	}
	
	
	template<typename U>
	inline bool enqueue(U&& item)
	{
		idx_t nodeIdx = allocate_node_for(std::forward<U>(item));
		
		auto tail_ = tail.load(std::memory_order_relaxed);
		while (!tail.compare_exchange_weak(tail_, nodeIdx, std::memory_order_release, std::memory_order_relaxed))
			continue;
		get_node_at(tail_)->next.store(nodeIdx, std::memory_order_release);
		
		return true;
	}
	
	inline bool try_dequeue(T& item)
	{
		while (true) {
			auto rawHead_ = head.load(std::memory_order_acquire);
			auto head_ = clear_consumed_flag(rawHead_);
			auto headNode = get_node_at(head_);
			auto next = headNode->next.load(std::memory_order_relaxed);
			if (next == 0) {
				// Can't move head (that would make head null), but can try to dequeue the node at head anyway
				if (is_consumed(rawHead_)) {
					return false;
				}
				
				if (head.compare_exchange_strong(head_, set_consumed_flag(head_), std::memory_order_release, std::memory_order_relaxed)) {
					// Whee, we own the right to dequeue this item
					item = std::move(*headNode->item());
					headNode->item()->~T();
					return true;
				}
			}
			else {
				// Remove node whether it's already been consumed or not; if it hasn't been consumed, consume it!
				
				// head_->next can't possibly change, since once it's not null nobody writes to it (and ABA is avoided with versioning)
				if (head.compare_exchange_weak(rawHead_, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
					// Aha, we successfully moved the head. But does it have anything in it?
					if (!is_consumed(rawHead_)) {
						item = std::move(*headNode->item());
						headNode->item()->~T();
					}
					
					add_node_to_free_list(head_, headNode);
					
					if (!is_consumed(rawHead_)) {
						return true;
					}
				}
			}
		}
	}
	
	// Dummy token methods (not used)
	bool enqueue(producer_token_t const&, T const&) { return false; }
	bool try_enqueue(producer_token_t, T const&) { return false; }
	bool try_dequeue(consumer_token_t, T& item) { return false; }
	template<typename It> bool enqueue_bulk(It, size_t) { return false; }
	template<typename It> bool enqueue_bulk(producer_token_t const&, It, size_t) { return false; }
	template<typename It> size_t try_dequeue_bulk(It, size_t) { return 0; }
	template<typename It> size_t try_dequeue_bulk(consumer_token_t, It, size_t) { return 0; }
	
private:
	struct Node
	{
		std::atomic<idx_t> next;
		
		alignas(T)
		char rawItem[sizeof(T)];
		
		template<typename U>
		Node(U&& item)
			: next(0)
		{
			new (this->item()) T(std::forward<U>(item));
		}
		
		Node()
			: next(0)
		{
		}
		
		inline T* item() { return reinterpret_cast<T*>(rawItem); }
	};
	
	struct Block
	{
		alignas(Node)
		char nodes[sizeof(Node) * BLOCK_SIZE];
		
		inline char* node_pos(idx_t idx) { return nodes + ((idx & BLOCK_MASK) >> BLOCK_SHIFT) * sizeof(Node); }
	};
	
	template<typename TSubBlock, int BlockSize>
	struct HigherOrderBlock
	{
		std::atomic<TSubBlock*> subblocks[BlockSize];
		
		HigherOrderBlock()
		{
			for (int i = 0; i != BlockSize; ++i) {
				subblocks[i].store(nullptr, std::memory_order_release);
			}
		}
		
		~HigherOrderBlock()
		{
			for (int i = 0; i != BlockSize; ++i) {
				if (subblocks[i].load(std::memory_order_relaxed) != nullptr) {
					delete subblocks[i].load(std::memory_order_relaxed);
				}
			}
		}
	};
	
	typedef HigherOrderBlock<Block, SUPER_BLOCK_SIZE> SuperBlock;
	typedef HigherOrderBlock<SuperBlock, ULTRA_BLOCK_SIZE> UltraBlock;
	typedef HigherOrderBlock<UltraBlock, UBER_BLOCK_SIZE> UberBlock;
	typedef HigherOrderBlock<UberBlock, UBER_BLOCKS> UberBlockContainer;
	
	
private:
	inline idx_t set_consumed_flag(idx_t idx)
	{
		return idx | (idx_t)1;
	}
	
	inline idx_t clear_consumed_flag(idx_t idx)
	{
		return idx & ~(idx_t)1;
	}
	
	inline bool is_consumed(idx_t idx)
	{
		return (idx & 1) != 0;
	}
	
	
	inline void add_node_to_free_list(idx_t idx, Node* node)
	{
		auto head = freeListHead.load(std::memory_order_relaxed);
		do {
			node->next.store(head, std::memory_order_relaxed);
		} while (!freeListHead.compare_exchange_weak(head, idx, std::memory_order_release, std::memory_order_relaxed));
	}
	
	inline idx_t try_get_node_from_free_list()
	{
		auto head = freeListHead.load(std::memory_order_acquire);
		while (head != 0 && !freeListHead.compare_exchange_weak(head, get_node_at(head)->next.load(std::memory_order_relaxed), std::memory_order_acquire)) {
			continue;
		}
		
		if (head != 0) {
			// Increment version
			head = (head & ~VERSION_MASK) | ((head + VERSION_INCR) & VERSION_MASK);
		}
		return head;
	}
	
	
	inline Node* get_node_at(idx_t idx)
	{
		auto uberBlock = uberBlockContainer.subblocks[(idx & UBER_BLOCK_IDX_MASK) >> UBER_BLOCK_IDX_SHIFT].load(std::memory_order_relaxed);
		auto ultraBlock = uberBlock->subblocks[(idx & UBER_BLOCK_MASK) >> UBER_BLOCK_SHIFT].load(std::memory_order_relaxed);
		auto superBlock = ultraBlock->subblocks[(idx & ULTRA_BLOCK_MASK) >> ULTRA_BLOCK_SHIFT].load(std::memory_order_relaxed);
		auto block = superBlock->subblocks[(idx & SUPER_BLOCK_MASK) >> SUPER_BLOCK_SHIFT].load(std::memory_order_relaxed);
		return reinterpret_cast<Node*>(block->node_pos(idx));
	}
	
	template<typename U>
	inline idx_t allocate_node_for(U&& item)
	{
		auto idx = try_get_node_from_free_list();
		if (idx != 0) {
			auto node = get_node_at(idx);
			node->next.store(0, std::memory_order_relaxed);
			new (node->item()) T(std::forward<U>(item));
			return idx;
		}
		new (new_node_address(idx)) Node(std::forward<U>(item));
		return idx;
	}
	
	inline idx_t allocate_blank_node()
	{
		idx_t idx;
		new (new_node_address(idx)) Node();
		return idx;
	}
	
	inline char* new_node_address(idx_t& idx)
	{
		idx = nextNodeIdx.fetch_add(static_cast<idx_t>(1) << BLOCK_SHIFT, std::memory_order_relaxed);
		
		std::size_t uberBlockContainerIdx = (idx & UBER_BLOCK_IDX_MASK) >> UBER_BLOCK_IDX_SHIFT;
		std::size_t uberBlockIdx = (idx & UBER_BLOCK_MASK) >> UBER_BLOCK_SHIFT;
		std::size_t ultraBlockIdx = (idx & ULTRA_BLOCK_MASK) >> ULTRA_BLOCK_SHIFT;
		std::size_t superBlockIdx = (idx & SUPER_BLOCK_MASK) >> SUPER_BLOCK_SHIFT;
		
		auto uberBlock = lookup_subblock<UberBlockContainer, UberBlock>(&uberBlockContainer, uberBlockContainerIdx);
		auto ultraBlock = lookup_subblock<UberBlock, UltraBlock>(uberBlock, uberBlockIdx);
		auto superBlock = lookup_subblock<UltraBlock, SuperBlock>(ultraBlock, ultraBlockIdx);
		auto block = lookup_subblock<SuperBlock, Block>(superBlock, superBlockIdx);
		return block->node_pos(idx);
	}
	
	template<typename TBlock, typename TSubBlock>
	inline TSubBlock* lookup_subblock(TBlock* block, std::size_t idx)
	{
		auto ptr = block->subblocks[idx].load(std::memory_order_acquire);
		if (ptr == nullptr) {
			auto newBlock = new TSubBlock();
			if (!block->subblocks[idx].compare_exchange_strong(ptr, newBlock, std::memory_order_release, std::memory_order_acquire)) {
				delete newBlock;
			}
			else {
				ptr = newBlock;
			}
		}
		return ptr;
	}
	
private:
	std::atomic<idx_t> nextNodeIdx;
	std::atomic<idx_t> head;
	std::atomic<idx_t> tail;
	std::atomic<idx_t> freeListHead;
	
	UberBlockContainer uberBlockContainer;
};
