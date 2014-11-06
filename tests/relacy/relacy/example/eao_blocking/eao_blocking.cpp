/*
#define BOOST_ALL_NO_LIB
#pragma warning (push, 3)
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "C:\boost_1_35_0\libs\thread\src\win32\exceptions.cpp"
#pragma warning (pop)


class business_logic
{
public:
    typedef unsigned account_id_t;
    typedef double balance_t;

    bool add_account(account_id_t acc_id, balance_t balance)
    {
        accounts_guard.lock();
        if (accounts.find(acc_id) != accounts.end())
        {
            accounts_guard.unlock();
            return false;
        }
        accounts[acc_id].balance = balance;
        accounts_guard.unlock();
        return true;
    }

    bool transfer_balance(account_id_t acc_id1, account_id_t acc_id2, balance_t amount)
    {
        accounts_guard.lock_shared();
        if (accounts.find(acc_id1) != accounts.end()
            || accounts.find(acc_id2) != accounts.end())
        {
            accounts_guard.unlock_shared();
            return false;
        }
        account_info& acc1 = accounts[acc_id1];
        account_info& acc2 = accounts[acc_id2];
        acc1.mtx.lock();
        acc2.mtx.lock();
        accounts_guard.unlock_shared();

        acc1.balance -= amount;
        acc2.balance += amount;

        acc1.mtx.unlock();
        acc2.mtx.unlock();
        return true;
    }

private:
    struct account_info
    {
        balance_t balance;
        boost::mutex mtx;

        account_info()
            : balance()
        {}

        account_info(account_info const& acc)
            : balance(acc.balance)
        {}
    };

    typedef std::map<account_id_t, account_info> account_map_t;
    account_map_t accounts;
    boost::shared_mutex accounts_guard;
};

*/

/*
#undef RL_TEST

#ifndef RL_TEST
//#   define ASSERT assert
typedef boost::mutex mutex_t;
#   define $$
#else
//#   define ASSERT RL_ASSERT
typedef rl::recursive_mutex mutex_t;
#   define $$ $
#endif



class business_logic
{
public:
    typedef unsigned account_id_t;
    typedef double balance_t;

    bool add_account(account_id_t acc_id, balance_t balance)
    {
        accounts_guard.lock($$);
        if (accounts.find(acc_id) != accounts.end())
        {
            accounts_guard.unlock($$);
            return false;
        }
        accounts[acc_id].balance = balance;
        accounts_guard.unlock($$);
        return true;
    }

    bool transfer_balance(account_id_t acc_id1, account_id_t acc_id2, balance_t amount)
    {
        accounts_guard.lock($$);
        if (accounts.find(acc_id1) == accounts.end()
            || accounts.find(acc_id2) == accounts.end())
        {
            accounts_guard.unlock($$);
            return false;
        }
        account_info& acc1 = accounts[acc_id1];
        account_info& acc2 = accounts[acc_id2];
        acc1.mtx.lock($$);
        acc2.mtx.lock($$);
        accounts_guard.unlock($$);

        acc1.balance -= amount;
        acc2.balance += amount;

        acc1.mtx.unlock($$);
        acc2.mtx.unlock($$);

        return true;
    }

private:
    struct account_info
    {
        balance_t balance;
        mutex_t mtx;

        account_info()
            : balance()
        {}

        account_info(account_info const& acc)
            : balance(acc.balance)
        {}
    };

    typedef std::map<account_id_t, account_info> account_map_t;
    account_map_t accounts;
    mutex_t accounts_guard;
};

*/

/*
struct business_logic_test : rl::test_suite<business_logic_test, 2>
{
    business_logic bl;

    static size_t const account_count = 10;

    void before()
    {
        for (size_t i = 0; i != account_count; ++i)
        {
            bool rv = bl.add_account(i, i * 10.0);
            RL_ASSERT(rv);
        }
    }

    void thread(unsigned)
    {
        business_logic::account_id_t acc1 = rl::rand(account_count);
        business_logic::account_id_t acc2 = rl::rand(account_count);
        bool rv = bl.transfer_balance(acc1, acc2, 1.0);
        RL_ASSERT(rv);
    }
};
*/
