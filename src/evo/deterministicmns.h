// Copyright (c) 2017 The Polis Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POLIS_DETERMINISTICMNS_H
#define POLIS_DETERMINISTICMNS_H

#include "evodb.h"
#include "providertx.h"
#include "dbwrapper.h"
#include "sync.h"
#include "spork.h"

#include "immer/map.hpp"
#include "immer/map_transient.hpp"

#include <map>

#include <boost/range/adaptors.hpp>
#include <boost/range/any_range.hpp>

class CBlock;
class CBlockIndex;
class CValidationState;

class CDeterministicMNState
{
public:
    int registeredHeight{-1};
    int lastPaidHeight{0};
    int PoSePenality{0};
    int PoSeRevivedHeight{-1};
    int PoSeBanHeight{-1};
    uint16_t revocationReason{CProUpRevTx::REASON_NOT_SPECIFIED};

    CKeyID keyIDOwner;
    CKeyID keyIDOperator;
    CKeyID keyIDVoting;
    CService addr;
    int32_t nProtocolVersion;
    CScript scriptPayout;
    CScript scriptOperatorPayout;

public:
    CDeterministicMNState() {}
    CDeterministicMNState(const CProRegTx& proTx)
    {
        keyIDOwner = proTx.keyIDOwner;
        keyIDOperator = proTx.keyIDOperator;
        keyIDVoting = proTx.keyIDVoting;
        addr = proTx.addr;
        nProtocolVersion = proTx.nProtocolVersion;
        scriptPayout = proTx.scriptPayout;
    }
    template<typename Stream>
    CDeterministicMNState(deserialize_type, Stream& s) { s >> *this;}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(registeredHeight);
        READWRITE(lastPaidHeight);
        READWRITE(PoSePenality);
        READWRITE(PoSeRevivedHeight);
        READWRITE(PoSeBanHeight);
        READWRITE(revocationReason);
        READWRITE(keyIDOwner);
        READWRITE(keyIDOperator);
        READWRITE(keyIDVoting);
        READWRITE(addr);
        READWRITE(nProtocolVersion);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
    }

    void ResetOperatorFields()
    {
        keyIDOperator.SetNull();
        addr = CService();
        nProtocolVersion = 0;
        scriptOperatorPayout = CScript();
        revocationReason = CProUpRevTx::REASON_NOT_SPECIFIED;
    }
    void BanIfNotBanned(int height)
    {
        if (PoSeBanHeight == -1) {
            PoSeBanHeight = height;
        }
    }

    bool operator==(const CDeterministicMNState& rhs) const
    {
        return registeredHeight == rhs.registeredHeight &&
               lastPaidHeight == rhs.lastPaidHeight &&
               PoSePenality == rhs.PoSePenality &&
               PoSeRevivedHeight == rhs.PoSeRevivedHeight &&
               PoSeBanHeight == rhs.PoSeBanHeight &&
               revocationReason == rhs.revocationReason &&
               keyIDOwner == rhs.keyIDOwner &&
               keyIDOperator == rhs.keyIDOperator &&
               keyIDVoting == rhs.keyIDVoting &&
               addr == rhs.addr &&
               nProtocolVersion == rhs.nProtocolVersion &&
               scriptPayout == rhs.scriptPayout &&
               scriptOperatorPayout == rhs.scriptOperatorPayout;
    }

    bool operator!=(const CDeterministicMNState& rhs) const
    {
        return !(rhs == *this);
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicMNState> CDeterministicMNStatePtr;
typedef std::shared_ptr<const CDeterministicMNState> CDeterministicMNStateCPtr;

class CDeterministicMN
{
public:
    CDeterministicMN() {}
    CDeterministicMN(const uint256& _proTxHash, const CProRegTx& _proTx)
    {
        proTxHash = _proTxHash;
        nCollateralIndex = _proTx.nCollateralIndex;
        operatorReward = _proTx.operatorReward;
        state = std::make_shared<CDeterministicMNState>(_proTx);
    }
    template<typename Stream>
    CDeterministicMN(deserialize_type, Stream& s) { s >> *this;}

    uint256 proTxHash;
    uint32_t nCollateralIndex;
    uint16_t operatorReward;
    CDeterministicMNStateCPtr state;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(proTxHash);
        READWRITE(nCollateralIndex);
        READWRITE(operatorReward);
        READWRITE(state);
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicMN> CDeterministicMNPtr;
typedef std::shared_ptr<const CDeterministicMN> CDeterministicMNCPtr;

class CDeterministicMNListDiff;

template<typename Stream, typename K, typename T, typename Hash, typename Equal>
void SerializeImmerMap(Stream& os, const immer::map<K, T, Hash, Equal>& m)
{
    WriteCompactSize(os, m.size());
    for (typename immer::map<K, T, Hash, Equal>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
        Serialize(os, (*mi));
}

template<typename Stream, typename K, typename T, typename Hash, typename Equal>
void UnserializeImmerMap(Stream& is, immer::map<K, T, Hash, Equal>& m)
{
    m = immer::map<K, T, Hash, Equal>();
    unsigned int nSize = ReadCompactSize(is);
    for (unsigned int i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize(is, item);
        m = m.set(item.first, item.second);
    }
}

class CDeterministicMNList
{
public:
    typedef immer::map<uint256, CDeterministicMNCPtr> MnMap;
    typedef immer::map<uint256, std::pair<uint256, uint32_t>> MnUniquePropertyMap;

private:
    uint256 blockHash;
    int height{-1};
    MnMap mnMap;

    // map of unique properties like address and keys
    // we keep track of this as checking for duplicates would otherwise be painfully slow
    // the entries in the map are ref counted as some properties might appear multiple times per MN (e.g. operator/owner keys)
    MnUniquePropertyMap mnUniquePropertyMap;

public:
    CDeterministicMNList() {}
    explicit CDeterministicMNList(const uint256& _blockHash, int _height) :
            blockHash(_blockHash),
            height(_height)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHash);
        READWRITE(height);
        if (ser_action.ForRead()) {
            UnserializeImmerMap(s, mnMap);
            UnserializeImmerMap(s, mnUniquePropertyMap);
        } else {
            SerializeImmerMap(s, mnMap);
            SerializeImmerMap(s, mnUniquePropertyMap);
        }
    }

public:

    size_t size() const
    {
        return mnMap.size();
    }

    typedef boost::any_range<const CDeterministicMNCPtr&, boost::forward_traversal_tag> range_type;

    range_type all_range() const
    {
        return boost::adaptors::transform(mnMap, [] (const MnMap::value_type& p) -> const CDeterministicMNCPtr& {
            return p.second;
        });
    }

    range_type valid_range() const
    {
        return boost::adaptors::filter(all_range(), [&] (const CDeterministicMNCPtr& dmn) -> bool {
            return IsMNValid(dmn);
        });
    }

    size_t all_count() const
    {
        return mnMap.size();
    }

    size_t valid_count() const
    {
        size_t c = 0;
        for (const auto& p : mnMap) {
            if (IsMNValid(p.second)) {
                c++;
            }
        }
        return c;
    }

public:
    const uint256& GetBlockHash() const
    {
        return blockHash;
    }
    void SetBlockHash(const uint256& _blockHash)
    {
        blockHash = _blockHash;
    }
    int GetHeight() const
    {
        return height;
    }
    void SetHeight(int _height)
    {
        height = _height;
    }

    bool IsMNValid(const uint256& proTxHash) const;
    bool IsMNPoSeBanned(const uint256& proTxHash) const;

    bool HasMN(const uint256& proTxHash) const {
        return GetMN(proTxHash) != nullptr;
    }
    CDeterministicMNCPtr GetMN(const uint256& proTxHash) const;
    CDeterministicMNCPtr GetValidMN(const uint256& proTxHash) const;
    CDeterministicMNCPtr GetMNByOperatorKey(const CKeyID& keyID);
    CDeterministicMNCPtr GetMNPayee() const;

    /**
     * Calculates the projected MN payees for the next *count* blocks. The result is not guaranteed to be correct
     * as PoSe banning might occur later
     * @param count
     * @return
     */
    std::vector<CDeterministicMNCPtr> GetProjectedMNPayees(int count) const;

    CDeterministicMNListDiff BuildDiff(const CDeterministicMNList& to) const;
    CDeterministicMNList ApplyDiff(const CDeterministicMNListDiff& diff) const;

    void AddMN(const CDeterministicMNCPtr &dmn)
    {
        assert(!mnMap.find(dmn->proTxHash));
        mnMap = mnMap.set(dmn->proTxHash, dmn);
        AddUniqueProperty(dmn, dmn->state->addr);
        AddUniqueProperty(dmn, dmn->state->keyIDOwner);
        AddUniqueProperty(dmn, dmn->state->keyIDOperator);
    }
    void UpdateMN(const uint256 &proTxHash, const CDeterministicMNStateCPtr &state)
    {
        auto oldDmn = mnMap.find(proTxHash);
        assert(oldDmn != nullptr);
        auto dmn = std::make_shared<CDeterministicMN>(**oldDmn);
        auto oldState = dmn->state;
        dmn->state = state;
        mnMap = mnMap.set(proTxHash, dmn);

        UpdateUniqueProperty(dmn, oldState->addr, state->addr);
        UpdateUniqueProperty(dmn, oldState->keyIDOwner, state->keyIDOwner);
        UpdateUniqueProperty(dmn, oldState->keyIDOperator, state->keyIDOperator);
    }
    void RemoveMN(const uint256& proTxHash)
    {
        auto dmn = GetMN(proTxHash);
        assert(dmn != nullptr);
        DeleteUniqueProperty(dmn, dmn->state->addr);
        DeleteUniqueProperty(dmn, dmn->state->keyIDOwner);
        DeleteUniqueProperty(dmn, dmn->state->keyIDOperator);
        mnMap = mnMap.erase(proTxHash);
    }

    template<typename T>
    bool HasUniqueProperty(const T& v) const
    {
        return mnUniquePropertyMap.count(::SerializeHash(v)) != 0;
    }
    template<typename T>
    CDeterministicMNCPtr GetUniquePropertyMN(const T& v) const
    {
        auto p = mnUniquePropertyMap.find(::SerializeHash(v));
        if (!p) {
            return nullptr;
        }
        return GetMN(p->first);
    }

private:
    bool IsMNValid(const CDeterministicMNCPtr& dmn) const;
    bool IsMNPoSeBanned(const CDeterministicMNCPtr& dmn) const;

    template<typename T>
    void AddUniqueProperty(const CDeterministicMNCPtr& dmn, const T& v)
    {
        auto hash = ::SerializeHash(v);
        auto oldEntry = mnUniquePropertyMap.find(hash);
        assert(!oldEntry || oldEntry->first == dmn->proTxHash);
        std::pair<uint256, uint32_t> newEntry(dmn->proTxHash, 1);
        if (oldEntry) {
            newEntry.second = oldEntry->second + 1;
        }
        mnUniquePropertyMap = mnUniquePropertyMap.set(hash, newEntry);
    }
    template<typename T>
    void DeleteUniqueProperty(const CDeterministicMNCPtr& dmn, const T& oldValue)
    {
        auto oldHash = ::SerializeHash(oldValue);
        auto p = mnUniquePropertyMap.find(oldHash);
        assert(p && p->first == dmn->proTxHash);
        if (p->second == 1) {
            mnUniquePropertyMap = mnUniquePropertyMap.erase(oldHash);
        } else {
            mnUniquePropertyMap = mnUniquePropertyMap.set(oldHash, std::make_pair(dmn->proTxHash, p->second - 1));
        }
    }
    template<typename T>
    void UpdateUniqueProperty(const CDeterministicMNCPtr& dmn, const T& oldValue, const T& newValue)
    {
        if (oldValue == newValue) {
            return;
        }
        DeleteUniqueProperty(dmn, oldValue);
        AddUniqueProperty(dmn, newValue);
    }
};

class CDeterministicMNListDiff
{
public:
    uint256 prevBlockHash;
    uint256 blockHash;
    int height{-1};
    std::map<uint256, CDeterministicMNCPtr> addedMNs;
    std::map<uint256, CDeterministicMNStateCPtr> updatedMNs;
    std::set<uint256> removedMns;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prevBlockHash);
        READWRITE(blockHash);
        READWRITE(height);
        READWRITE(addedMNs);
        READWRITE(updatedMNs);
        READWRITE(removedMns);
    }

public:
    bool HasChanges() const
    {
        return !addedMNs.empty() || !updatedMNs.empty() || !removedMns.empty();
    }
};

class CDeterministicMNManager
{
    static const int SNAPSHOT_LIST_PERIOD = 576; // once per day
    static const int LISTS_CACHE_SIZE = 576;

public:
    CCriticalSection cs;

private:
    CEvoDB& evoDb;

    std::map<uint256, CDeterministicMNList> mnListsCache;
    int tipHeight{-1};
    uint256 tipBlockHash;

public:
    CDeterministicMNManager(CEvoDB& _evoDb);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    // the returned list will not contain the correct block hash (we can't know it yet as the coinbase TX is not updated yet)
    bool BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state, CDeterministicMNList& mnListRet);

    CDeterministicMNList GetListForBlock(const uint256& blockHash);
    CDeterministicMNList GetListAtChainTip();

    CDeterministicMNCPtr GetMN(const uint256& blockHash, const uint256& proTxHash);
    bool HasValidMNAtBlock(const uint256& blockHash, const uint256& proTxHash);
    bool HasValidMNAtChainTip(const uint256& proTxHash);

    bool IsDeterministicMNsSporkActive(int height = -1);

private:
    void UpdateSpork15Value();
    int64_t GetSpork15Value();
    void CleanupCache(int height);
};

extern CDeterministicMNManager* deterministicMNManager;

#endif//POLIS_DETERMINISTICMNS_H