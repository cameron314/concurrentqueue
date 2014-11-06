#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"

// THE TEST IS EXPECTED TO FAIL WITH "DEADLOCK"

class CondVar
{
public:
        CondVar();
        ~CondVar();
   void Enter();
   void Wait();
   void Release();
   void ReleaseAll();
   void Leave();

private:
    std::atomic<int>        m_lMutex;
    std::atomic<unsigned>   m_dwWaitingForSignal;
    HANDLE                  m_xhEvtEnter;
    HANDLE                  m_xhSemRelease;
};

CondVar::CondVar()
    : m_xhEvtEnter(CreateEvent(0, 0, 0, 0))
    , m_xhSemRelease(CreateSemaphore(0, 0, 0x7FFFFFFF, 0))
{
    m_lMutex.store(0, std::memory_order_relaxed);
    m_dwWaitingForSignal.store(0, std::memory_order_relaxed);
}

CondVar::~CondVar()
{
    CloseHandle(m_xhEvtEnter);
    CloseHandle(m_xhSemRelease);
}

void CondVar::Enter()
{
   int lMutex = m_lMutex.load(std::memory_order_seq_cst);
   for (;;)
   {
     if( lMutex >= 0 )
     {
         if (m_lMutex.compare_exchange_weak(lMutex, lMutex | 0x80000000u, std::memory_order_seq_cst))
            break;
     }
     else
     {
        if (false == m_lMutex.compare_exchange_weak(lMutex, lMutex + 1, std::memory_order_seq_cst))
            continue;
        WaitForSingleObject(m_xhEvtEnter, INFINITE);
        RL_ASSERT(m_lMutex.load(std::memory_order_seq_cst) < 0);
        break;
     }
   }
}

void CondVar::Wait()
{
    unsigned dwWaitingForSignal = m_dwWaitingForSignal.load(std::memory_order_seq_cst);
    m_dwWaitingForSignal.store(dwWaitingForSignal + 1, std::memory_order_seq_cst);
    RL_ASSERT(m_lMutex.load(std::memory_order_seq_cst) < 0);

    int lMutex = m_lMutex.load(std::memory_order_seq_cst);
    for (;;)
    {
        unsigned dwWaitingToOwn = lMutex & 0x7FFFFFFFu;
        RL_ASSERT(dwWaitingToOwn >= dwWaitingForSignal);
        if (dwWaitingToOwn == dwWaitingForSignal)
        {
            if (m_lMutex.compare_exchange_weak(lMutex, dwWaitingToOwn + 1, std::memory_order_seq_cst))
                break;
        }
        else
        {
            SetEvent(m_xhEvtEnter);
            break;
       }
   }

   WaitForSingleObject(m_xhSemRelease, INFINITE);
   WaitForSingleObject(m_xhEvtEnter, INFINITE);

   RL_ASSERT(m_lMutex.load(std::memory_order_seq_cst) < 0);
}

void CondVar::Release()
{
   RL_ASSERT(m_lMutex.load(std::memory_order_seq_cst) < 0);
    unsigned dwWaitingForSignal = m_dwWaitingForSignal.load(std::memory_order_seq_cst);
    if (dwWaitingForSignal != 0)
    {
        m_dwWaitingForSignal.store(dwWaitingForSignal - 1, std::memory_order_seq_cst);
        ReleaseSemaphore(m_xhSemRelease, 1, 0);
    }
}

void CondVar::ReleaseAll()
{
   RL_ASSERT(m_lMutex.load(std::memory_order_seq_cst) < 0);
    unsigned dwWaitingForSignal = m_dwWaitingForSignal.load(std::memory_order_seq_cst);
    if (dwWaitingForSignal != 0)
    {
        m_dwWaitingForSignal.store(0, std::memory_order_seq_cst);
        ReleaseSemaphore(m_xhSemRelease, dwWaitingForSignal, 0);
   }
}

void CondVar::Leave()
{
    int lMutex = m_lMutex.load(std::memory_order_seq_cst);
    RL_ASSERT(lMutex < 0);
    for (;;)
    {
        unsigned dwWaitingToOwn     = lMutex & 0x7FFFFFFFu;
        unsigned dwWaitingForSignal = m_dwWaitingForSignal.load(std::memory_order_seq_cst);
        RL_ASSERT(dwWaitingToOwn >= dwWaitingForSignal);
        if (dwWaitingToOwn == dwWaitingForSignal)
        {
            if (m_lMutex.compare_exchange_weak(lMutex, lMutex & 0x7FFFFFFF, std::memory_order_seq_cst))
                break;
        }
        else
        {
            if (false == m_lMutex.compare_exchange_weak(lMutex, lMutex - 1, std::memory_order_seq_cst))
                continue;
            SetEvent(m_xhEvtEnter);
            break;
        }
    } 
}

struct CondVarTest : rl::test_suite<CondVarTest, 3>
{
    VAR_T(int) stage;
    CondVar cv;

    void before()
    {
        VAR(stage) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            cv.Enter();
            VAR(stage) += 1;
            cv.ReleaseAll();
            while (VAR(stage) != 2)
                cv.Wait();
            cv.Leave();
        }
        else if (1 == index)
        {
            cv.Enter();
            while (VAR(stage) != 1)
                cv.Wait();
            VAR(stage) += 1;
            cv.ReleaseAll();
            cv.Leave();
        }
        else if (2 == index)
        {
            cv.Enter();
            while (VAR(stage) != 2)
                cv.Wait();
            cv.Leave();
        }
    }
};




int main()
{
    rl::simulate<CondVarTest>();
}

