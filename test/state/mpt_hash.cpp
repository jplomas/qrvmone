// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "mpt_hash.hpp"
#include "account.hpp"
#include "mpt.hpp"
#include "rlp.hpp"
#include "state.hpp"

namespace qrvmone::state
{
namespace
{
/// The 32-byte keccak256 digest held in the low half of a 64-byte hash256 container.
bytes_view digest(const hash256& h) noexcept
{
    return {h.bytes + 32, 32};
}

/// Computes the storage trie root of one account.
hash256 mpt_hash(const std::unordered_map<hash256, StorageValue>& storage)
{
    // go-qrl keys the storage trie by keccak256 of the 32-byte storage key
    // (core/state/state_object.go passes key[:] of a common.Hash to
    // StateTrie.UpdateStorage, whose hashKey yields a 32-byte path). Storage keys
    // are the low 256 bits of the 64-byte slot operand, so hash that half only.
    MPT trie;
    for (const auto& [key, value] : storage)
    {
        if (!is_zero(value.current))  // Skip "deleted" values.
            trie.insert(digest(keccak256(digest(key))), rlp::encode(rlp::trim(value.current)));
    }
    return trie.hash();
}
}  // namespace

/// Computes the state trie root of the given accounts.
hash256 mpt_hash(const std::unordered_map<address, Account>& accounts)
{
    MPT trie;
    for (const auto& [addr, acc] : accounts)
    {
        // hash256 (= bytes64) carries the 32-byte keccak digest in its low half.
        // go-qrl keys the account trie by the 32-byte keccak256 of the 64-byte
        // address (trie/secure_trie.go hashKey), and the account RLP's
        // storage_root and code_hash fields are 32 bytes, so slice to the low
        // half for the key and for both fields.
        const auto storage_root = mpt_hash(acc.storage);
        const auto code_hash = keccak256(acc.code);
        trie.insert(digest(keccak256(addr)),
            rlp::encode_tuple(acc.nonce, acc.balance, digest(storage_root), digest(code_hash)));
    }
    return trie.hash();
}

/// Computes the transactions trie root: keys are the RLP of the index, values the
/// typed transaction RLP.
hash256 mpt_hash(std::span<const Transaction> transactions)
{
    MPT trie;
    for (size_t i = 0; i < transactions.size(); ++i)
        trie.insert(rlp::encode(i), rlp::encode(transactions[i]));
    return trie.hash();
}

/// Computes the receipts trie root: keys are the RLP of the index, values the typed
/// receipt RLP.
hash256 mpt_hash(std::span<const TransactionReceipt> receipts)
{
    MPT trie;
    for (size_t i = 0; i < receipts.size(); ++i)
        trie.insert(rlp::encode(i), rlp::encode(receipts[i]));

    return trie.hash();
}

}  // namespace qrvmone::state
