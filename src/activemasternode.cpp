// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "masternode.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "netbase.h"
#include "protocol.h"
#include "warnings.h"
#include "init.h"
#include "netbase.h"
#include "evo/deterministicmns.h"

// Keep track of the active Masternode
CActiveMasternodeInfo activeMasternode;
CActiveLegacyMasternodeManager legacyActiveMasternodeManager;
CActiveDeterministicMasternodeManager* activeMasternodeManager;

std::string CActiveDeterministicMasternodeManager::GetStateString() const
{
    switch (state) {
        case MASTERNODE_WAITING_FOR_PROTX:  return "WAITING_FOR_PROTX";
        case MASTERNODE_POSE_BANNED:        return "POSE_BANNED";
        case MASTERNODE_REMOVED:            return "REMOVED";
        case MASTERNODE_READY:              return "READY";
        case MASTERNODE_ERROR:              return "ERROR";
        default:                            return "UNKNOWN";
    }
}

std::string CActiveDeterministicMasternodeManager::GetStatus() const
{
    switch (state) {
        case MASTERNODE_WAITING_FOR_PROTX:  return "Waiting for ProTx to appear on-chain";
        case MASTERNODE_POSE_BANNED:        return "Masternode was PoSe banned";
        case MASTERNODE_REMOVED:            return "Masternode removed from list";
        case MASTERNODE_READY:              return "Ready";
        case MASTERNODE_ERROR:              return "Error. " + strError;
        default:                            return "Unknown";
    }
}

void CActiveDeterministicMasternodeManager::Init() {
    LOCK(cs_main);

    if (!fMasternodeMode)
        return;

    if (!deterministicMNManager->IsDeterministicMNsSporkActive())
        return;

    if (!GetLocalAddress(activeMasternode.service)) {
        state = MASTERNODE_ERROR;
        return;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();

    CDeterministicMNCPtr dmn = mnList.GetMNByOperatorKey(activeMasternode.keyIDOperator);
    if (!dmn) {
        // MN not appeared on the chain yet
        return;
    }

    if (!mnList.IsMNValid(dmn->proTxHash)) {
        if (mnList.IsMNPoSeBanned(dmn->proTxHash)) {
            state = MASTERNODE_POSE_BANNED;
        } else {
            state = MASTERNODE_REMOVED;
        }
        return;
    }

    mnListEntry = dmn;

    LogPrintf("CActiveDeterministicMasternodeManager::Init -- proTxHash=%s, proTx=%s\n", mnListEntry->proTxHash.ToString(), mnListEntry->ToString());

    if (activeMasternode.service != mnListEntry->state->addr) {
        state = MASTERNODE_ERROR;
        strError = "Local address does not match the address from ProTx";
        LogPrintf("CActiveDeterministicMasternodeManager::Init -- ERROR: %s", strError);
        return;
    }

    if (mnListEntry->state->nProtocolVersion != PROTOCOL_VERSION) {
        state = MASTERNODE_ERROR;
        strError = "Local protocol version does not match version from ProTx. You may need to update the ProTx";
        LogPrintf("CActiveDeterministicMasternodeManager::Init -- ERROR: %s", strError);
        return;
    }

    activeMasternode.outpoint = COutPoint(mnListEntry->proTxHash, mnListEntry->nCollateralIndex);
    state = MASTERNODE_READY;
}

void CActiveDeterministicMasternodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) {
    LOCK(cs_main);

    if (!fMasternodeMode)
        return;

    if (!deterministicMNManager->IsDeterministicMNsSporkActive(pindexNew->nHeight)) {
        return;
    }

    if (state == MASTERNODE_WAITING_FOR_PROTX) {
        Init();
    } else if (state == MASTERNODE_READY) {
        if (!deterministicMNManager->HasValidMNAtBlock(pindexNew->GetBlockHash(), mnListEntry->proTxHash)) {
            // MN disappeared from MN list
            state = MASTERNODE_REMOVED;
            activeMasternode.outpoint.SetNull();
            // MN might have reappeared in same block with a new ProTx (with same masternode key)
            Init();
        }
    } else if (state == MASTERNODE_REMOVED || state == MASTERNODE_POSE_BANNED) {
        // MN might have reappeared with a new ProTx (with same masternode key)
        Init();
    }
}

bool CActiveDeterministicMasternodeManager::GetLocalAddress(CService &addrRet) {
    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(addrRet) && CMasternode::IsValidNetAddr(addrRet);
    if (!fFoundLocal && Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }
    if(!fFoundLocal) {
        strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveDeterministicMasternodeManager::GetLocalAddress -- ERROR: %s\n", strError);
        return false;
    }
    return true;
}

/********* LEGACY *********/

void CActiveLegacyMasternodeManager::ManageState(CConnman& connman)
{
    if (deterministicMNManager->IsDeterministicMNsSporkActive())
        return;

    LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageState -- Start\n");
    if(!fMasternodeMode) {
        LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageState -- Not a masternode, returning\n");
        return;
    }
    if(Params().NetworkIDString() != CBaseChainParams::REGTEST && !masternodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveLegacyMasternodeManager::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_MASTERNODE_INITIAL;
    }

    LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(eType == MASTERNODE_UNKNOWN) {
        ManageStateInitial(connman);
    }

    if(eType == MASTERNODE_REMOTE) {
        ManageStateRemote();
    }

    SendMasternodePing(connman);
}

std::string CActiveLegacyMasternodeManager::GetStateString() const
{
    switch (nState) {
        case ACTIVE_MASTERNODE_INITIAL:         return "INITIAL";
        case ACTIVE_MASTERNODE_SYNC_IN_PROCESS: return "SYNC_IN_PROCESS";
        case ACTIVE_MASTERNODE_INPUT_TOO_NEW:   return "INPUT_TOO_NEW";
        case ACTIVE_MASTERNODE_NOT_CAPABLE:     return "NOT_CAPABLE";
        case ACTIVE_MASTERNODE_STARTED:         return "STARTED";
        default:                                return "UNKNOWN";
    }
}

std::string CActiveLegacyMasternodeManager::GetStatus() const
{
    switch (nState) {
        case ACTIVE_MASTERNODE_INITIAL:         return "Node just started, not yet activated";
        case ACTIVE_MASTERNODE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Masternode";
        case ACTIVE_MASTERNODE_INPUT_TOO_NEW:   return strprintf("Masternode input must have at least %d confirmations", Params().GetConsensus().nMasternodeMinimumConfirmations);
        case ACTIVE_MASTERNODE_NOT_CAPABLE:     return "Not capable masternode: " + strNotCapableReason;
        case ACTIVE_MASTERNODE_STARTED:         return "Masternode successfully started";
        default:                                return "Unknown";
    }
}

std::string CActiveLegacyMasternodeManager::GetTypeString() const
{
    std::string strType;
    switch(eType) {
    case MASTERNODE_REMOTE:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveLegacyMasternodeManager::SendMasternodePing(CConnman& connman)
{
    if (deterministicMNManager->IsDeterministicMNsSporkActive())
        return false;

    if(!fPingerEnabled) {
        LogPrint("masternode", "CActiveLegacyMasternodeManager::SendMasternodePing -- %s: masternode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!mnodeman.Has(activeMasternode.outpoint)) {
        strNotCapableReason = "Masternode not in masternode list";
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        LogPrintf("CActiveLegacyMasternodeManager::SendMasternodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CMasternodePing mnp(activeMasternode.outpoint);
    mnp.nSentinelVersion = nSentinelVersion;
    mnp.fSentinelIsCurrent =
            (abs(GetAdjustedTime() - nSentinelPingTime) < MASTERNODE_SENTINEL_PING_MAX_SECONDS);
    if(!mnp.Sign(activeMasternode.keyOperator, activeMasternode.keyIDOperator)) {
        LogPrintf("CActiveLegacyMasternodeManager::SendMasternodePing -- ERROR: Couldn't sign Masternode Ping\n");
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    if(mnodeman.IsMasternodePingedWithin(activeMasternode.outpoint, MASTERNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveLegacyMasternodeManager::SendMasternodePing -- Too early to send Masternode Ping\n");
        return false;
    }

    mnodeman.SetMasternodeLastPing(activeMasternode.outpoint, mnp);

    LogPrintf("CActiveLegacyMasternodeManager::SendMasternodePing -- Relaying ping, collateral=%s\n", activeMasternode.outpoint.ToStringShort());
    mnp.Relay(connman);

    return true;
}

bool CActiveLegacyMasternodeManager::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    nSentinelPingTime = GetAdjustedTime();

    return true;
}

void CActiveLegacyMasternodeManager::ManageStateInitial(CConnman& connman)
{
    if (deterministicMNManager->IsDeterministicMNsSporkActive())
        return;

    LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(activeMasternode.service) && CMasternode::IsValidNetAddr(activeMasternode.service);
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        connman.ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty, this](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(activeMasternode.service, &pnode->addr) && CMasternode::IsValidNetAddr(activeMasternode.service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if (!fFoundLocal && Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        if (Lookup("127.0.0.1", activeMasternode.service, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }

    if(!fFoundLocal) {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(activeMasternode.service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", activeMasternode.service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if(activeMasternode.service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", activeMasternode.service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        // Check socket connectivity
        LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- Checking inbound connection to '%s'\n", activeMasternode.service.ToString());
        SOCKET hSocket;
        bool fConnected = ConnectSocket(activeMasternode.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Could not connect to " + activeMasternode.service.ToString();
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }
    // Default to REMOTE
    eType = MASTERNODE_REMOTE;

    LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveLegacyMasternodeManager::ManageStateRemote()
{
    if (deterministicMNManager->IsDeterministicMNsSporkActive())
        return;

    LogPrint("masternode", "CActiveLegacyMasternodeManager::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, keyIDOperator = %s\n",
             GetStatus(), GetTypeString(), fPingerEnabled, activeMasternode.keyIDOperator.ToString());

    mnodeman.CheckMasternode(activeMasternode.keyIDOperator, true);
    masternode_info_t infoMn;
    if(mnodeman.GetMasternodeInfo(activeMasternode.keyIDOperator, infoMn)) {
        if(infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(activeMasternode.service != infoMn.addr) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this masternode changed recently.";
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CMasternode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Masternode in %s state", CMasternode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        auto dmn = deterministicMNManager->GetListAtChainTip().GetMN(infoMn.outpoint.hash);
        if (dmn) {
            if (dmn->state->keyIDOperator != infoMn.keyIDOperator) {
                nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
                strNotCapableReason = strprintf("Masternode collateral is a ProTx and masternode key does not match key from -masternodeprivkey");
                LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            if (dmn->state->addr != infoMn.addr) {
                nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
                strNotCapableReason = strprintf("Masternode collateral is a ProTx and ProTx address does not match local address");
                LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- Collateral is a ProTx\n");
        }
        if(nState != ACTIVE_MASTERNODE_STARTED) {
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- STARTED!\n");
            activeMasternode.outpoint = infoMn.outpoint;
            activeMasternode.service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_MASTERNODE_STARTED;
        } else if (activeMasternode.outpoint != infoMn.outpoint) {
            LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- STARTED with new collateral %s!\n", infoMn.outpoint.ToStringShort());
            activeMasternode.outpoint = infoMn.outpoint;
        }
    }
    else {
        nState = ACTIVE_MASTERNODE_NOT_CAPABLE;
        strNotCapableReason = "Masternode not in masternode list";
        LogPrintf("CActiveLegacyMasternodeManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}
