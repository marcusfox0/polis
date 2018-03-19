// Copyright (c) 2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "providertx.h"
#include "specialtx.h"
#include "deterministicmns.h"

#include "hash.h"
#include "clientversion.h"
#include "streams.h"
#include "messagesigner.h"
#include "chainparams.h"
#include "validation.h"
#include "univalue.h"
#include "core_io.h"
#include "script/standard.h"
#include "base58.h"

template <typename ProTx>
static bool CheckService(const uint256& proTxHash, const ProTx& proTx, const CBlockIndex* pindex, CValidationState& state)
{
    if (proTx.nProtocolVersion < MIN_PROTX_PROTO_VERSION || proTx.nProtocolVersion > MAX_PROTX_PROTO_VERSION)
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-proto-version");

    if (!proTx.addr.IsValid())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-addr");
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !proTx.addr.IsRoutable())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-addr");

    if (pindex) {
        auto mnList = deterministicMNManager->GetListAtHeight(pindex->nHeight - 1);
        for (const auto& dmn : mnList.all_range()) {
            if (dmn->state->addr == proTx.addr && dmn->proTxHash != proTxHash)
                return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-addr");
        }
    }
    return true;
}

template <typename ProTx>
static bool CheckInputsHashAndSig(const CTransaction &tx, const ProTx& proTx, const CKeyID &keyID, CValidationState& state)
{
    uint256 inputsHash = CalcTxInputsHash(tx);
    if (inputsHash != proTx.inputsHash)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-inputs-hash");

    std::string strError;
    if (!CHashSigner::VerifyHash(::SerializeHash(proTx), keyID, proTx.vchSig, strError))
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig", false, strError);

    return true;
}

bool CheckProRegTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    AssertLockHeld(cs_main);

    CProRegTx ptx;
    if (!GetTxPayload(tx, ptx))
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-payload");

    if (ptx.nVersion != CProRegTx::CURRENT_VERSION)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");

    if (ptx.nCollateralIndex >= tx.vout.size())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-index");
    if (tx.vout[ptx.nCollateralIndex].nValue != 1000 * COIN)
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral");
    if (ptx.keyIDOwner.IsNull() || ptx.keyIDOperator.IsNull() || ptx.keyIDVoting.IsNull())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-null");
    // we may support P2SH later, but restrict it for now (while in transitioning phase from old MN list to deterministic list)
    if (!ptx.scriptPayout.IsPayToPublicKeyHash())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");

    CTxDestination payoutDest;
    if (!ExtractDestination(ptx.scriptPayout, payoutDest)) {
        // should not happen as we checked script types before
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }
    // don't allow reuse of keys for different purposes
    if (payoutDest == CTxDestination(ptx.keyIDOwner) || payoutDest == CTxDestination(ptx.keyIDOperator) || payoutDest == CTxDestination(ptx.keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }

    // This is a temporary restriction that will be lifted later
    // It is required while we are transitioning from the old MN list to the deterministic list
    if (tx.vout[ptx.nCollateralIndex].scriptPubKey != ptx.scriptPayout)
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-collateral");

    // It's allowed to set addr/protocolVersion to 0, which will put the MN into PoSe-banned state and require a ProUpServTx to be issues later
    // If any of both is set, it must be valid however
    if ((ptx.addr != CService() || ptx.nProtocolVersion != 0) && !CheckService(tx.GetHash(), ptx, pindex, state))
        return false;

    if (ptx.operatorReward > 10000)
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-reward");

    if (pindex) {
        auto mnList = deterministicMNManager->GetListAtHeight(pindex->nHeight - 1);
        std::set<CKeyID> keyIDs;
        for (const auto& dmn : mnList.all_range()) {
            keyIDs.emplace(dmn->state->keyIDOwner);
            keyIDs.emplace(dmn->state->keyIDOperator);
        }
        if (keyIDs.count(ptx.keyIDOwner) || keyIDs.count(ptx.keyIDOperator)) {
            return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-key");
        }

        if (!deterministicMNManager->IsDeterministicMNsSporkActive(pindex->nHeight)) {
            if (ptx.keyIDOwner != ptx.keyIDOperator || ptx.keyIDOwner != ptx.keyIDVoting) {
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-not-same");
            }
        }
    }

    if (!CheckInputsHashAndSig(tx, ptx, ptx.keyIDOwner, state))
        return false;
    return true;
}

bool CheckProUpServTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    AssertLockHeld(cs_main);

    CProUpServTx ptx;
    if (!GetTxPayload(tx, ptx))
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-payload");

    if (ptx.nVersion != CProRegTx::CURRENT_VERSION)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");

    if (!CheckService(ptx.proTxHash, ptx, pindex, state))
        return false;

    if (pindex) {
        auto mn = deterministicMNManager->GetMN(pindex->nHeight - 1, ptx.proTxHash);
        if (!mn)
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");

        if (ptx.scriptOperatorPayout != CScript()) {
            if (mn->operatorReward == 0) {
                // don't allow to set operator reward payee in case no operatorReward was set
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee");
            }
            // we may support P2SH later, but restrict it for now (while in transitioning phase from old MN list to deterministic list)
            if (!ptx.scriptOperatorPayout.IsPayToPublicKeyHash())
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee");
        }

        // we can only check the signature if pindex != NULL and the MN is known
        if (!CheckInputsHashAndSig(tx, ptx, mn->state->keyIDOperator, state))
            return false;
    }

    return true;
}

bool CheckProUpRegTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    AssertLockHeld(cs_main);

    CProUpRegTx ptx;
    if (!GetTxPayload(tx, ptx))
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-payload");

    if (ptx.nVersion != CProRegTx::CURRENT_VERSION)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");

    if (ptx.keyIDOperator.IsNull() || ptx.keyIDVoting.IsNull())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-null");
    // we may support P2SH later, but restrict it for now (while in transitioning phase from old MN list to deterministic list)
    if (!ptx.scriptPayout.IsPayToPublicKeyHash())
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");

    CTxDestination payoutDest;
    if (!ExtractDestination(ptx.scriptPayout, payoutDest)) {
        // should not happen as we checked script types before
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }

    if (pindex) {
        auto mnList = deterministicMNManager->GetListAtHeight(pindex->nHeight - 1);
        auto dmn = mnList.GetMN(ptx.proTxHash);
        if (!dmn)
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");

        // don't allow reuse of keys for different purposes
        if (payoutDest == CTxDestination(dmn->state->keyIDOwner) || payoutDest == CTxDestination(ptx.keyIDOperator) || payoutDest == CTxDestination(ptx.keyIDVoting)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
        }

        // This is a temporary restriction that will be lifted later
        // It is required while we are transitioning from the old MN list to the deterministic list
        CTransactionRef proRegTx;
        uint256 tmpHashBlock;
        if (!GetTransaction(ptx.proTxHash, proRegTx, Params().GetConsensus(), tmpHashBlock))
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-payee-collateral");
        if (proRegTx->vout[dmn->nCollateralIndex].scriptPubKey != ptx.scriptPayout)
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-collateral");

        std::map<CKeyID, CDeterministicMNCPtr> keyIDs;
        for (const auto& dmn2 : mnList.all_range()) {
            keyIDs.emplace(dmn2->state->keyIDOwner, dmn2);
            keyIDs.emplace(dmn2->state->keyIDOperator, dmn2);
        }
        if (keyIDs.count(ptx.keyIDOperator) && (ptx.proTxHash != keyIDs[ptx.keyIDOperator]->proTxHash || ptx.keyIDOperator != keyIDs[ptx.keyIDOperator]->state->keyIDOperator)) {
            return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-key");
        }

        if (!deterministicMNManager->IsDeterministicMNsSporkActive(pindex->nHeight)) {
            if (dmn->state->keyIDOwner != ptx.keyIDOperator || dmn->state->keyIDOwner != ptx.keyIDVoting) {
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-not-same");
            }
        }

        if (!CheckInputsHashAndSig(tx, ptx, dmn->state->keyIDOwner, state))
            return false;
    }

    return true;
}

bool CheckProUpRevTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    AssertLockHeld(cs_main);

    CProUpRevTx ptx;
    if (!GetTxPayload(tx, ptx))
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-payload");

    if (ptx.nVersion != CProRegTx::CURRENT_VERSION)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");

    if (ptx.reason < CProUpRevTx::REASON_NOT_SPECIFIED || ptx.reason > CProUpRevTx::REASON_LAST)
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-reason");

    if (pindex) {
        auto mnList = deterministicMNManager->GetListAtHeight(pindex->nHeight - 1);
        auto dmn = mnList.GetMN(ptx.proTxHash);
        if (!dmn)
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");

        if (!CheckInputsHashAndSig(tx, ptx, dmn->state->keyIDOperator, state))
            return false;
    }

    return true;
}

std::string CProRegTx::ToString() const
{
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptPayout, dest)) {
        payee = CBitcoinAddress(dest).ToString();
    }

    return strprintf("CProRegTx(nVersion=%d, nProtocolVersion=%d, nCollateralIndex=%d, addr=%s, operatorReward=%f, keyIDOwner=%s, keyIDOperator=%s, keyIDVoting=%s, scriptPayout=%s)",
        nVersion, nProtocolVersion, nCollateralIndex, addr.ToString(), (double)operatorReward / 100, keyIDOwner.ToString(), keyIDOperator.ToString(), keyIDVoting.ToString(), payee);
}

void CProRegTx::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("version", nVersion));
    obj.push_back(Pair("protocolVersion", nProtocolVersion));
    obj.push_back(Pair("collateralIndex", (int)nCollateralIndex));
    obj.push_back(Pair("service", addr.ToString(false)));
    obj.push_back(Pair("keyIDOwner", keyIDOwner.ToString()));
    obj.push_back(Pair("keyIDOperator", keyIDOperator.ToString()));
    obj.push_back(Pair("keyIDVoting", keyIDVoting.ToString()));

    CTxDestination dest;
    if (ExtractDestination(scriptPayout, dest)) {
        CBitcoinAddress bitcoinAddress(dest);
        obj.push_back(Pair("payoutAddress", bitcoinAddress.ToString()));
    }
    obj.push_back(Pair("operatorReward", (double)operatorReward / 100));

    obj.push_back(Pair("inputsHash", inputsHash.ToString()));
}

std::string CProUpServTx::ToString() const
{
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        payee = CBitcoinAddress(dest).ToString();
    }

    return strprintf("CProUpServTx(nVersion=%d, proTxHash=%s, nProtocolVersion=%d, addr=%s, operatorPayoutAddress=%s)",
                     nVersion, proTxHash.ToString(), nProtocolVersion, addr.ToString(), payee);
}

void CProUpServTx::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("version", nVersion));
    obj.push_back(Pair("proTxHash", proTxHash.ToString()));
    obj.push_back(Pair("protocolVersion", nProtocolVersion));
    obj.push_back(Pair("service", addr.ToString(false)));
    CTxDestination dest;
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        CBitcoinAddress bitcoinAddress(dest);
        obj.push_back(Pair("operatorPayoutAddress", bitcoinAddress.ToString()));
    }
    obj.push_back(Pair("inputsHash", inputsHash.ToString()));
}

std::string CProUpRegTx::ToString() const
{
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptPayout, dest)) {
        payee = CBitcoinAddress(dest).ToString();
    }

    return strprintf("CProUpRegTx(nVersion=%d, proTxHash=%s, keyIDOperator=%s, keyIDVoting=%s, payoutAddress=%s)",
                     nVersion, proTxHash.ToString(), keyIDOperator.ToString(), keyIDVoting.ToString(), payee);
}

void CProUpRegTx::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("version", nVersion));
    obj.push_back(Pair("proTxHash", proTxHash.ToString()));
    obj.push_back(Pair("keyIDOperator", keyIDOperator.ToString()));
    obj.push_back(Pair("keyIDVoting", keyIDVoting.ToString()));
    CTxDestination dest;
    if (ExtractDestination(scriptPayout, dest)) {
        CBitcoinAddress bitcoinAddress(dest);
        obj.push_back(Pair("payoutAddress", bitcoinAddress.ToString()));
    }
    obj.push_back(Pair("inputsHash", inputsHash.ToString()));
}

std::string CProUpRevTx::ToString() const
{
    return strprintf("CProUpRevTx(nVersion=%d, proTxHash=%s, reason=%d)",
                     nVersion, proTxHash.ToString(), reason);
}

void CProUpRevTx::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("version", nVersion));
    obj.push_back(Pair("proTxHash", proTxHash.ToString()));
    obj.push_back(Pair("reason", (int)reason));
    obj.push_back(Pair("inputsHash", inputsHash.ToString()));
}

bool IsProTxCollateral(const CTransaction& tx, uint32_t n)
{
    return GetProTxCollateralIndex(tx) == n;
}

uint32_t GetProTxCollateralIndex(const CTransaction& tx)
{
    if (tx.nVersion < 3 || tx.nType != TRANSACTION_PROVIDER_REGISTER)
        return (uint32_t) - 1;
    CProRegTx proTx;
    if (!GetTxPayload(tx, proTx))
        assert(false);
    return proTx.nCollateralIndex;
}
