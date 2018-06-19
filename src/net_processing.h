// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include "net.h"
#include "validationinterface.h"
class CTxDSIn : public CTxIn
{
public:
    bool fHasSig; // flag to indicate if signed
    int nSentTimes; //times we've sent this anonymously

    CTxDSIn(const CTxIn& txin) :
            CTxIn(txin),
            fHasSig(false),
            nSentTimes(0)
    {}

    CTxDSIn() :
            CTxIn(),
            fHasSig(false),
            nSentTimes(0)
    {}
};

class CDarksendQueue
{
public:
    int nDenom;
    CTxIn vin;
    int64_t nTime;
    bool fReady; //ready for submit
    std::vector<unsigned char> vchSig;
    // memory only
    bool fTried;

    CDarksendQueue() :
            nDenom(0),
            vin(CTxIn()),
            nTime(0),
            fReady(false),
            vchSig(std::vector<unsigned char>()),
            fTried(false)
    {}

    CDarksendQueue(int nDenom, CTxIn vin, int64_t nTime, bool fReady) :
            nDenom(nDenom),
            vin(vin),
            nTime(nTime),
            fReady(fReady),
            vchSig(std::vector<unsigned char>()),
            fTried(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nDenom);
        READWRITE(vin);
        READWRITE(nTime);
        READWRITE(fReady);
        READWRITE(vchSig);
    }

    /** Sign this mixing transaction
     *  \return true if all conditions are met:
     *     1) we have an active Masternode,
     *     2) we have a valid Masternode private key,
     *     3) we signed the message successfully, and
     *     4) we verified the message successfully
     */
    bool Sign();
    /// Check if we have a valid Masternode address
    bool CheckSignature(const CPubKey& pubKeyMasternode);

    bool Relay();

    /// Is this queue expired?
    bool IsExpired() { return GetTime() - nTime > PRIVATESEND_QUEUE_TIMEOUT; }

    std::string ToString()
    {
        return strprintf("nDenom=%d, nTime=%lld, fReady=%s, fTried=%s, masternode=%s",
                         nDenom, nTime, fReady ? "true" : "false", fTried ? "true" : "false", vin.prevout.ToStringShort());
    }

    friend bool operator==(const CDarksendQueue& a, const CDarksendQueue& b)
    {
        return a.nDenom == b.nDenom && a.vin.prevout == b.vin.prevout && a.nTime == b.nTime && a.fReady == b.fReady;
    }
};

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Expiration time for orphan transactions in seconds */
static const int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
/** Minimum time between orphan transactions expire time checks in seconds */
static const int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;

/** Headers download timeout expressed in microseconds
 *  Timeout = base + per_header * (expected number of headers) */
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_BASE = 15 * 60 * 1000000; // 15 minutes
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_PER_HEADER = 1000; // 1ms/header

/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);
class CDarksendBroadcastTx
{
public:
    CTransaction tx;
    CTxIn vin;
    std::vector<unsigned char> vchSig;
    int64_t sigTime;

    CDarksendBroadcastTx() :
            tx(CTransaction()),
            vin(CTxIn()),
            vchSig(std::vector<unsigned char>()),
            sigTime(0)
    {}

    CDarksendBroadcastTx(CTransaction tx, CTxIn vin, int64_t sigTime) :
            tx(tx),
            vin(vin),
            vchSig(std::vector<unsigned char>()),
            sigTime(sigTime)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(tx);
        READWRITE(vin);
        READWRITE(vchSig);
        READWRITE(sigTime);
    }

    friend bool operator==(const CDarksendBroadcastTx& a, const CDarksendBroadcastTx& b)
    {
        return a.tx == b.tx;
    }
    friend bool operator!=(const CDarksendBroadcastTx& a, const CDarksendBroadcastTx& b)
    {
        return !(a == b);
    }
    explicit operator bool() const
    {
        return *this != CDarksendBroadcastTx();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode);
};


class PeerLogicValidation : public CValidationInterface {
private:
    CConnman* connman;
    class CDarksendBroadcastTx
    {
    public:
        CTransaction tx;
        CTxIn vin;
        std::vector<unsigned char> vchSig;
        int64_t sigTime;

        CDarksendBroadcastTx() :
                tx(CTransaction()),
                vin(CTxIn()),
                vchSig(std::vector<unsigned char>()),
                sigTime(0)
        {}

        CDarksendBroadcastTx(CTransaction tx, CTxIn vin, int64_t sigTime) :
                tx(tx),
                vin(vin),
                vchSig(std::vector<unsigned char>()),
                sigTime(sigTime)
        {}

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
            READWRITE(tx);
            READWRITE(vin);
            READWRITE(vchSig);
            READWRITE(sigTime);
        }

        friend bool operator==(const CDarksendBroadcastTx& a, const CDarksendBroadcastTx& b)
        {
            return a.tx == b.tx;
        }
        friend bool operator!=(const CDarksendBroadcastTx& a, const CDarksendBroadcastTx& b)
        {
            return !(a == b);
        }
        explicit operator bool() const
        {
            return *this != CDarksendBroadcastTx();
        }

        bool Sign();
        bool CheckSignature(const CPubKey& pubKeyMasternode);
    };

public:
    PeerLogicValidation(CConnman* connmanIn);

    virtual void SyncTransaction(const CTransaction& tx, const CBlockIndex* pindex, int nPosInBlock) override;
    virtual void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    virtual void BlockChecked(const CBlock& block, const CValidationState& state) override;
    virtual void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& pblock) override;
};

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);

/** Process protocol messages received from a given node */
bool ProcessMessages(CNode* pfrom, CConnman& connman, const std::atomic<bool>& interrupt);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   connman         The connection manager for that node.
 * @param[in]   interrupt       Interrupt condition for processing threads
 * @return                      True if there is more work to be done
 */
bool SendMessages(CNode* pto, CConnman& connman, const std::atomic<bool>& interrupt);

#endif // BITCOIN_NET_PROCESSING_H
