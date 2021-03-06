/* slave_banker.h                                                  -*- C++ -*-
   Jeremy Barens, 8 November 2012
   Copyright (c) 2012 Datacratic Inc.  All rights reserved.

   Slave "banker" object that authorizes spend.
*/

#ifndef __banker__slave_banker_h__
#define __banker__slave_banker_h__

#include <atomic>
#include "banker.h"
#include "application_layer.h"
#include "soa/service/zmq_endpoint.h"
#include "soa/service/typed_message_channel.h"
#include "jml/arch/spinlock.h"
#include <thread>
#include <atomic>

namespace RTBKIT {


/*****************************************************************************/
/* SLAVE BUDGET CONTROLLER                                                   */
/*****************************************************************************/

/** Budget controller object that operates as a slave of the master banker.
 */

struct SlaveBudgetController
    : public BudgetController, public Accountant, public MessageLoop  {

    SlaveBudgetController();

    ~SlaveBudgetController()
    {
        shutdown();
    }


    void setApplicationLayer(const std::shared_ptr<ApplicationLayer> &layer)
    {
        ExcCheck(layer != nullptr, "Layer can not be null");
        applicationLayer = layer;
        addSource("SlaveBudgetController::ApplicationLayer", *layer);
    }

    virtual void addAccount(const AccountKey & account,
                            const OnBudgetResult & onResult);

    virtual void topupTransfer(const AccountKey & account,
                               CurrencyPool amount,
                               const OnBudgetResult & onResult);

    virtual void setBudget(const std::string & topLevelAccount,
                           CurrencyPool amount,
                           const OnBudgetResult & onResult);
    
    virtual void addBudget(const std::string & topLevelAccount,
                           CurrencyPool amount,
                           const OnBudgetResult & onResult);

    virtual void
    getAccountList(const AccountKey & prefix,
                   int depth,
                   std::function<void (std::exception_ptr,
                                       std::vector<AccountKey> &&)> onResult);

    virtual void
    getAccountSummary(const AccountKey & account,
                     int depth,
                     std::function<void (std::exception_ptr,
                                         AccountSummary &&)> onResult);

    virtual void
    getAccount(const AccountKey & account,
               std::function<void (std::exception_ptr,
                                   Account &&)> onResult);

    static std::shared_ptr<HttpClientSimpleCallbacks>
    budgetResultCallback(const SlaveBudgetController::OnBudgetResult & onResult);
private:
    std::shared_ptr<ApplicationLayer> applicationLayer;
    //std::shared_ptr<HttpClient> httpClient;
};


/*****************************************************************************/
/* SLAVE BANKER                                                              */
/*****************************************************************************/

/** Banker object that operates as a slave of the master banker.  Splits a
    big block of budget into individual auctions and keeps track of
    what has been committed so far.
*/
struct SlaveBanker : public Banker, public MessageLoop {
    static const CurrencyPool DefaultSpendRate;

    SlaveBanker();

    ~SlaveBanker()
    {
        shutdown();
    }

    SlaveBanker(const std::string & accountSuffix,
                CurrencyPool spenRate = DefaultSpendRate);

    /** Initialize the slave banker.  

        The accountSuffix parameter is used to name spend accounts underneath
        the given budget accounts (to disambiguate between multiple
        accessors of those accounts).  It must be unique across the entire
        system, but should be consistent from one invocation to another.
    */
    void init(const std::string & accountSuffix,
              CurrencyPool spendRate = DefaultSpendRate);

    /** Notify the banker that we're going to need to be spending some
        money for the given account.  We also keep track of how much
        "float" we try to maintain for the account.
    */
    virtual void addSpendAccount(const AccountKey & account,
                                 CurrencyPool accountFloat,
                                 std::function<void (std::exception_ptr, ShadowAccount&&)> onDone);

    ShadowAccount getAccount(const AccountKey & account)
    {
        return accounts.activateAccount(account);
    }

    virtual bool authorizeBid(const AccountKey & account,
                              const std::string & item,
                              Amount amount)
    {
        return accounts.authorizeBid(account, item, amount);
    }

    virtual void commitBid(const AccountKey & account,
                           const std::string & item,
                           Amount amountPaid,
                           const LineItems & lineItems)
    {
        accounts.commitBid(account, item, amountPaid, lineItems);
    }

    virtual Amount detachBid(const AccountKey & account,
                             const std::string & item)
    {
        return accounts.detachBid(account, item);
    }

    virtual void attachBid(const AccountKey & account,
                           const std::string & item,
                           Amount amountAuthorized)
    {
        accounts.attachBid(account, item, amountAuthorized);
    }

    virtual void commitDetachedBid(const AccountKey & account,
                                   Amount amountAuthorized,
                                   Amount amountPaid,
                                   const LineItems & lineItems)
    {
        return accounts.commitDetachedBid(account,
                                          amountAuthorized, amountPaid,
                                          lineItems);
    }

    virtual void forceWinBid(const AccountKey & account,
                             Amount amountPaid,
                             const LineItems & lineItems)
    {
        return accounts.forceWinBid(account, amountPaid, lineItems);
    }

    /** Sync the given account synchronously, returning the new status of
        the account.
    */
    ShadowAccount syncAccountSync(const AccountKey & account);

    /** Sync the account asynchronously, calling the given callback once
        synchronization is done.
    */
    void syncAccount(const AccountKey & account,
                     std::function<void (std::exception_ptr,
                                         ShadowAccount &&)> onDone);

    /** Synchronize all accounts synchronously. */
    void syncAllSync();

    /** Synchronize all accounts asynchronously. */
    void syncAll(std::function<void (std::exception_ptr)> onDone
                 = std::function<void (std::exception_ptr)>());

    /** Testing only: get the internal state of an account. */
    ShadowAccount getAccountStateDebug(AccountKey accountKey) const
    {
        return accounts.getAccount(accountKey);
    }

    void setApplicationLayer(const std::shared_ptr<ApplicationLayer> &layer)
    {
        applicationLayer = layer;
        addSource("SlaveBanker::ApplicationLayer", *layer);
    }

    bool isReauthorizing() const
    {
        return reauthorizing;
    }

    void waitReauthorized() const;

    size_t getNumReauthorized()
        const
    {
        return numReauthorized;
    }

    double getLastReauthorizeDelay()
        const
    {
        return lastReauthorizeDelay;
    }

    /* Logging */
    virtual void logBidEvents(const Datacratic::EventRecorder & eventRecorder)
    {
        accounts.logBidEvents(eventRecorder);
    }

    /* Monitor */
    virtual MonitorIndicator getProviderIndicators() const;

private:    
    ShadowAccounts accounts;

    /// Channel to asynchronously keep track of which accounts have been
    /// created and must therefore be synchronized
    TypedMessageSink<AccountKey> createdAccounts;
    std::string accountSuffix;

    std::shared_ptr<ApplicationLayer> applicationLayer;
    typedef ML::Spinlock Lock;
    mutable Lock syncLock;
    Datacratic::Date lastSync;

    
    /** Periodically we report spend to the banker.*/
    void reportSpend(uint64_t numTimeoutsExpired);
    Date reportSpendSent;

    /** Periodically we ask the banker to re-authorize our budget. */
    void reauthorizeBudget(uint64_t numTimeoutsExpired);
    Date reauthorizeBudgetSent;
    CurrencyPool spendRate;


    /// Called when we get an account status back from the master banker
    /// after a synchrnonization
    void onSyncResult(const AccountKey & accountKey,
                      std::function<void (std::exception_ptr,
                                          ShadowAccount &&)> onDone,
                      std::exception_ptr exc,
                      Account&& masterAccount);

    /// Called when we get an account status back from the master banker
    /// after an initialization
    void onInitializeResult(const AccountKey & accountKey,
                            std::function<void (std::exception_ptr,
                                                ShadowAccount &&)> onDone,
                            std::exception_ptr exc,
                            Account&& masterAccount);
    
    /// Called when we get a message back from the master after authorizing
    /// budget
    void onReauthorizeBudgetMessage(const AccountKey & account,
                                    std::exception_ptr exc,
                                    int responseCode,
                                    const std::string & payload);

    /** Return the name of our copy of a given shadow account */
    std::string getShadowAccountStr(const AccountKey & account) const
    {
        return account.childKey(accountSuffix).toString();
    }

    std::atomic<bool> shutdown_;

    std::atomic<bool> reauthorizing;
    Date reauthorizeDate;
    double lastReauthorizeDelay;
    size_t numReauthorized;
    size_t accountsLeft;
};

} // naemspace RTBKIT

#endif /* __banker__slave_banker_h__ */
