#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"



class business_logic
{
public:
    typedef unsigned account_id_t;
    typedef double balance_t;

    business_logic()
    {
        pthread_rwlock_init(&accounts_guard, 0);
    }

    ~business_logic()
    {
        pthread_rwlock_destroy(&accounts_guard);
    }

    bool add_account(account_id_t acc_id, balance_t balance)
    {
        pthread_rwlock_wrlock(&accounts_guard);
        if (accounts.find(acc_id) != accounts.end())
        {
            pthread_rwlock_unlock(&accounts_guard);
            return false;
        }
        accounts[acc_id].balance = balance;
        pthread_rwlock_unlock(&accounts_guard);
        return true;
    }

    bool transfer_balance(account_id_t acc_id1, account_id_t acc_id2, balance_t amount)
    {
        if (acc_id1 == acc_id2)
            return true;
        pthread_rwlock_rdlock(&accounts_guard);
        if (accounts.find(acc_id1) == accounts.end()
            || accounts.find(acc_id2) == accounts.end())
        {
            pthread_rwlock_unlock(&accounts_guard);
            return false;
        }
        account_info& acc1 = accounts[acc_id1];
        account_info& acc2 = accounts[acc_id2];
        if (acc_id1 > acc_id2)
        {
            pthread_mutex_lock(&acc1.mtx);
            pthread_mutex_lock(&acc2.mtx);
        }
        else
        {
            pthread_mutex_lock(&acc2.mtx);
            pthread_mutex_lock(&acc1.mtx);
        }
        pthread_rwlock_unlock(&accounts_guard);

        acc1.balance -= amount;
        acc2.balance += amount;

        pthread_mutex_unlock(&acc1.mtx);
        pthread_mutex_unlock(&acc2.mtx);
        return true;
    }

private:
    struct account_info
    {
        balance_t balance;
        pthread_mutex_t mtx;

        account_info()
            : balance()
        {
            pthread_mutex_init(&mtx, 0);
        }

        account_info(account_info const& acc)
            : balance(acc.balance)
        {
            pthread_mutex_init(&mtx, 0);
        }

        ~account_info()
        {
            pthread_mutex_destroy(&mtx);
        }
    };

    typedef std::map<account_id_t, account_info> account_map_t;
    account_map_t accounts;
    pthread_rwlock_t accounts_guard;
};




struct business_logic_test : rl::test_suite<business_logic_test, 2>
{
    business_logic bl;

    static size_t const account_count = 4;

    void before()
    {
        for (size_t i = 0; i != account_count; ++i)
        {
            bool rv = bl.add_account(i, i * 10.0);
            RL_ASSERT(rv);
        }
    }

    void thread(unsigned /*index*/)
    {
        business_logic::account_id_t acc1 = rl::rand(account_count);
        business_logic::account_id_t acc2 = rl::rand(account_count);
        bool rv = bl.transfer_balance(acc1, acc2, 1.0);
        RL_ASSERT(rv);
    }
};




int main()
{    
    rl::simulate<business_logic_test>();
}

