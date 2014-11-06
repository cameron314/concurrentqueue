#include "stdafx.h"
#include "../../relacy/relacy_cli.hpp"


using rl::nvar;
using rl::nvolatile;
using rl::mutex;




template<typename T>
class ws_deque
{
public:
    ws_deque()
    {
        m_mask($) = initial_size - 1;
        m_headIndex($) = 0;
        m_tailIndex($) = 0;
        m_array($) = new nvar<T> [initial_size];
        m_arraySize($) = initial_size;
    }

    bool IsEmpty()
    {
        return m_headIndex($) >= m_tailIndex($);
    }

    size_t Count()
    {
        return m_tailIndex($) - m_headIndex($);
    }

    void push(T item)
    {
        size_t tail = m_tailIndex($);
        if (tail <= m_headIndex($) + m_mask($))
        {
            m_array($)[tail & m_mask($)]($) = item;
            m_tailIndex($) = tail + 1;
        }
        else
        {
            m_foreignLock.lock($);
            size_t head = m_headIndex($);
            size_t count = Count();
            if (count >= m_mask($))
            {
                size_t arraySize = m_arraySize($);
                size_t mask = m_mask($);
                nvar<T>* newArray = new nvar<T> [arraySize * 2];
                nvar<T>* arr = m_array($);
                for (size_t i = 0; i != count; ++i)
                    newArray[i]($) = arr[(i + head) & mask]($);
                m_array($) = newArray;
                m_arraySize($) = arraySize * 2;
                m_headIndex($) = 0;
                m_tailIndex($) = count;
                tail = count;
                m_mask($) = (mask * 2) | 1;
            }
            m_array($)[tail & m_mask($)]($) = item;
            m_tailIndex($) = tail + 1;
            m_foreignLock.unlock($);
        }
    }

    bool pop(T& item)
    {
        size_t tail = m_tailIndex($);
        if (tail == 0)
            return false;
        tail -= 1;
        rl::Interlocked::Exchange(m_tailIndex, tail, $);
        if (m_headIndex($) <= tail)
        {
            item = m_array($)[tail & m_mask($)]($);
            return true;
        }
        else
        {
            m_foreignLock.lock($);
            if (m_headIndex($) <= tail)
            {
                item = m_array($)[tail & m_mask($)]($);
                m_foreignLock.unlock($);
                return true;
            }
            else
            {
                m_tailIndex($) = tail + 1;
                m_foreignLock.unlock($);
                return false;
            }
        }
    }

    bool steal(T& item)
    {
        if (false == m_foreignLock.try_lock($))
            return false;
        size_t head = m_headIndex($);
        rl::Interlocked::Exchange(m_headIndex, head + 1, $);
        if (head < m_tailIndex($))
        {
            item = m_array($)[head & m_mask($)]($);
            m_foreignLock.unlock($);
            return true;
        }
        else
        {
            m_headIndex($) = head;
            m_foreignLock.unlock($);
            return false;
        }
    }

private:
    static size_t const initial_size = 2;
    nvar<nvar<T>*> m_array;
    nvar<size_t> m_mask;
    nvar<size_t> m_arraySize;
    nvolatile<size_t> m_headIndex;
    nvolatile<size_t> m_tailIndex;
    mutex m_foreignLock;
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
    rl::simulate<ws_deque_test>();
}

