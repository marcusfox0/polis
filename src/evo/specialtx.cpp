// Copyright (c) 2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"
#include "primitives/block.h"
#include "consensus/validation.h"
#include "hash.h"
#include "clientversion.h"

#include "specialtx.h"
#include "providertx.h"

bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    if (tx.nVersion < 3 || tx.nType == TRANSACTION_NORMAL)
        return true;

    switch (tx.nType) {
        case TRANSACTION_PROVIDER_REGISTER:
            return CheckProRegTx(tx, pindex, state);
    }

    return state.DoS(100, false, REJECT_INVALID, "bad-tx-type");
}

bool ProcessSpecialTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    if (tx.nVersion < 3 || tx.nType == TRANSACTION_NORMAL)
        return true;

    switch (tx.nType) {
        case TRANSACTION_PROVIDER_REGISTER:
            return true; // handled in batches per block
    }

    return state.DoS(100, false, REJECT_INVALID, "bad-tx-type");
}

bool UndoSpecialTx(const CTransaction& tx, const CBlockIndex* pindex)
{
    if (tx.nVersion < 3 || tx.nType == TRANSACTION_NORMAL)
        return true;

    switch (tx.nType) {
        case TRANSACTION_PROVIDER_REGISTER:
            return true; // handled in batches per block
    }

    return false;
}

bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state)
{
    for (int i = 0; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];
        if (!CheckSpecialTx(tx, pindex, state))
            return false;
        if (!ProcessSpecialTx(tx, pindex, state))
            return false;
    }

    return true;
}

bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex)
{
    for (int i = (int)block.vtx.size() - 1; i >= 0; --i) {
        const CTransaction& tx = *block.vtx[i];
        if (!UndoSpecialTx(tx, pindex))
            return false;
    }
    return true;
}

uint256 CalcTxInputsHash(const CTransaction& tx)
{
    CHashWriter hw(CLIENT_VERSION, SER_GETHASH);
    for (const auto& in : tx.vin) {
        hw << in.prevout;
    }
    return hw.GetHash();
}
