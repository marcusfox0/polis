// Copyright (c) 2017 The Polis Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef POLIS_PROVIDERTX_H
#define POLIS_PROVIDERTX_H

#include "primitives/transaction.h"
#include "consensus/validation.h"

#include "netaddress.h"
#include "pubkey.h"

class CBlockIndex;
class UniValue;

class CProRegTx
{
public:
    static const int CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION}; // message version
    int32_t nProtocolVersion{0};
    uint32_t nCollateralIndex{(uint32_t) - 1};
    CService addr;
    CKeyID keyIDOwner;
    CKeyID keyIDOperator;
    CKeyID keyIDVoting;
    uint16_t operatorReward{0};
    CScript scriptPayout;
    uint256 inputsHash; // replay protection
    std::vector<unsigned char> vchSig;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nProtocolVersion);
        READWRITE(nCollateralIndex);
        READWRITE(addr);
        READWRITE(keyIDOwner);
        READWRITE(keyIDOperator);
        READWRITE(keyIDVoting);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(operatorReward);
        READWRITE(inputsHash);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

class CProUpServTx
{
public:
    static const int CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION}; // message version
    uint256 proTxHash;
    int32_t nProtocolVersion{0};
    CService addr;
    CScript scriptOperatorPayout;
    uint256 inputsHash; // replay protection
    std::vector<unsigned char> vchSig;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(nProtocolVersion);
        READWRITE(addr);
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
        READWRITE(inputsHash);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

class CProUpRegTx
{
public:
    static const int CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION}; // message version
    uint256 proTxHash;
    CKeyID keyIDOperator;
    CKeyID keyIDVoting;
    CScript scriptPayout;
    uint256 inputsHash; // replay protection
    std::vector<unsigned char> vchSig;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(keyIDOperator);
        READWRITE(keyIDVoting);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(inputsHash);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

class CProUpRevTx
{
public:
    static const int CURRENT_VERSION = 1;

    // these are just informational and do not have any effect on the revocation
    enum {
        REASON_NOT_SPECIFIED = 0,
        REASON_TERMINATION_OF_SERVICE = 1,
        REASON_COMPROMISED_KEYS = 2,
        REASON_CHANGE_OF_KEYS = 3,
        REASON_LAST = REASON_CHANGE_OF_KEYS
    };

public:
    uint16_t nVersion{CURRENT_VERSION}; // message version
    uint256 proTxHash;
    uint16_t reason{REASON_NOT_SPECIFIED};
    uint256 inputsHash; // replay protection
    std::vector<unsigned char> vchSig;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(reason);
        READWRITE(inputsHash);
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};


bool CheckProRegTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
bool CheckProUpServTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
bool CheckProUpRegTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
bool CheckProUpRevTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

bool IsProTxCollateral(const CTransaction& tx, uint32_t n);
uint32_t GetProTxCollateralIndex(const CTransaction& tx);

#endif//POLIS_PROVIDERTX_H