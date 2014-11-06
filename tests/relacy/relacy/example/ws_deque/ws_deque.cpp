#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"


using namespace std;
using rl::var;




template<typename T>
class ws_deque
{
public:
    ws_deque()
    {
        VAR(m_mask) = initial_size - 1;
        m_headIndex.store(0, memory_order_relaxed);
        m_tailIndex.store(0, memory_order_relaxed);
        VAR(m_array) = new atomic<T> [initial_size];
        VAR(m_arraySize) = initial_size;
    }

    ~ws_deque()
    {
        delete [] VAR(m_array);
    }

    bool IsEmpty() const
    {
        return m_headIndex.load(memory_order_acquire)
            >= m_tailIndex.load(memory_order_acquire);
    }

    size_t Count() const
    {
        return m_tailIndex.load(memory_order_acquire)
             - m_headIndex.load(memory_order_acquire);
    }

    void push(T item)
    {
        size_t tail = m_tailIndex.load(memory_order_acquire);
        if (tail < m_headIndex.load(memory_order_acquire) + VAR(m_mask))
        {
            VAR(m_array)[tail & VAR(m_mask)].store(item, memory_order_relaxed);
            m_tailIndex.store(tail + 1, memory_order_release);
        }
        else
        {
            m_foreignLock.lock($);
            size_t head = m_headIndex.load(memory_order_acquire);
            size_t count = Count();
            if (count >= VAR(m_mask))
            {
                size_t arraySize = m_arraySize($);
                size_t mask = VAR(m_mask);
                atomic<T>* newArray = new atomic<T> [arraySize * 2];
                atomic<T>* arr = m_array($);
                //!!! for (size_t i = 0; i != arraySize; ++i)
                for (size_t i = 0; i != count; ++i)
                    newArray[i].store(arr[(i + head) & mask].load(memory_order_seq_cst), memory_order_relaxed);
                delete [] VAR(m_array);
                VAR(m_array) = newArray;
                VAR(m_arraySize) = arraySize * 2;
                m_headIndex.store(0, memory_order_release);
                m_tailIndex.store(count, memory_order_release);
                tail = count;
                VAR(m_mask) = (mask * 2) | 1;
            }
            VAR(m_array)[tail & VAR(m_mask)].store(item, memory_order_relaxed);
            m_tailIndex.store(tail + 1, memory_order_release);
            m_foreignLock.unlock($);
        }
    }

    bool pop(T& item)
    {
        size_t tail = m_tailIndex.load(memory_order_acquire);
        if (tail == 0)
            return false;
        tail -= 1;
        m_tailIndex.store(tail, memory_order_release);
        atomic_thread_fence(memory_order_seq_cst);
        if (m_headIndex.load(memory_order_acquire) <= tail)
        {
            item = VAR(m_array)[tail & VAR(m_mask)].load(memory_order_relaxed);
            return true;
        }
        else
        {
            m_foreignLock.lock($);
            if (m_headIndex.load(memory_order_acquire) <= tail)
            {
                item = VAR(m_array)[tail & VAR(m_mask)].load(memory_order_relaxed);
                m_foreignLock.unlock($);
                return true;
            }
            else
            {
                m_tailIndex.store(tail + 1, memory_order_release);
                m_foreignLock.unlock($);
                return false;
            }
        }
    }

    bool steal(T& item)
    {
        if (false == m_foreignLock.try_lock($))
            return false;
        size_t head = m_headIndex.load(memory_order_acquire);
        m_headIndex.store(head + 1, memory_order_release);
        atomic_thread_fence(memory_order_seq_cst);
        if (head < m_tailIndex.load(memory_order_acquire))
        {
            item = VAR(m_array)[head & VAR(m_mask)].load(memory_order_relaxed);
            m_foreignLock.unlock($);
            return true;
        }
        else
        {
            m_headIndex.store(head, memory_order_release);
            m_foreignLock.unlock($);
            return false;
        }
    }

private:
    static size_t const initial_size = 2;
    var<atomic<T>*> m_array;
    var<size_t> m_mask;
    var<size_t> m_arraySize;
    atomic<size_t> m_headIndex;
    atomic<size_t> m_tailIndex;
    mutex m_foreignLock;
};




struct ws_deque_test0 : rl::test_suite<ws_deque_test0, 4>
{
    ws_deque<int> q;

    void before()
    {
    }

    void after()
    {
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
            }

            for (size_t i = 0; i != 5; ++i)
            {
                int p = 0;
                bool res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                int p = 0;
                bool res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                q.push(10);
                int p = 0;
                bool res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
                p = 0;
                res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push(10);
                q.push(10);
                q.push(10);
                int p = 0;
                bool res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
            }

            for (size_t i = 0; i != 14; ++i)
            {
                q.push(10);
                int p = 0;
                bool res = q.pop(p);
                RL_ASSERT(10 == p || false == res);
            }
        }
        else
        {
            for (size_t i = 0; i != 4; ++i)
            {
                int p = 0;
                bool res = q.steal(p);
                RL_ASSERT(10 == p || false == res);
            }
        }
    }
};




struct ws_deque_test : rl::test_suite<ws_deque_test, 2>
{
    ws_deque<int> q;
    bool state [2];

    void before()
    {
        state[0] = true;
        state[1] = true;
    }

    void after()
    {
        RL_ASSERT(state[0] == false);
        RL_ASSERT(state[1] == false);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            q.push(1);
            q.push(2);

            int item = 0;
            bool res = q.pop(item);
            RL_ASSERT(res && item == 2);
            RL_ASSERT(state[1]);
            state[1] = false;

            item = 0;
            res = q.pop(item);
            if (res)
            {
                RL_ASSERT(state[0]);
                state[0] = false;
            }

            item = 0;
            res = q.pop(item);
            RL_ASSERT(res == false);
        }
        else
        {
            int item = 0;
            bool res = q.steal(item);
            if (res)
            {
                RL_ASSERT(item == 1);
                RL_ASSERT(state[0]);
                state[0] = false;
            }
        }
    }
};




int main()
{
    rl::simulate<ws_deque_test0>();
    rl::simulate<ws_deque_test>();
}
 
