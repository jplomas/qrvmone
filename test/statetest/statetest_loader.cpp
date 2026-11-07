// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "../utils/stdx/utility.hpp"
#include "statetest.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>

namespace qrvmone::test
{
namespace json = nlohmann;
using qrvmc::from_hex;

/// Parses a hex string into a uint8_t; values above 0xff are an error.
template <>
uint8_t from_json<uint8_t>(const json::json& j)
{
    const auto ret = std::stoul(j.get<std::string>(), nullptr, 16);
    if (ret > std::numeric_limits<uint8_t>::max())
        throw std::out_of_range("from_json<uint8_t>: value > 0xFF");

    return static_cast<uint8_t>(ret);
}

/// Parses a JSON integer, or a string holding a decimal, octal or 0x hex integer.
template <typename T>
static std::optional<T> integer_from_json(const json::json& j)
{
    if (j.is_number_integer())
        return j.get<T>();

    if (!j.is_string())
        return {};

    const auto s = j.get<std::string>();
    size_t num_processed = 0;
    T v = 0;
    if constexpr (std::is_same_v<T, uint64_t>)
        v = std::stoull(s, &num_processed, 0);
    else
        v = std::stoll(s, &num_processed, 0);

    if (num_processed == 0 || num_processed != s.size())
        return {};
    return v;
}

/// Parses a signed 64-bit integer from a JSON number or integer string.
template <>
int64_t from_json<int64_t>(const json::json& j)
{
    const auto v = integer_from_json<int64_t>(j);
    if (!v.has_value())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    return *v;
}

/// Parses an unsigned 64-bit integer from a JSON number or integer string.
template <>
uint64_t from_json<uint64_t>(const json::json& j)
{
    const auto v = integer_from_json<uint64_t>(j);
    if (!v.has_value())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    return *v;
}

/// Parses a hex string, with or without 0x, into bytes.
template <>
bytes from_json<bytes>(const json::json& j)
{
    return from_hex(j.get<std::string>()).value();
}

/// Parses an address written as "Q" followed by exactly 128 hex characters.
template <>
address from_json<address>(const json::json& j)
{
    // go-qrl parses addresses with hexutil.UnmarshalFixedTextQ (common.Address.UnmarshalText):
    // "Q" followed by exactly 128 hex characters. Require the same here, so a fixture that
    // loads in one implementation loads in the other, instead of left-padding short forms.
    const auto s = j.get<std::string>();
    constexpr auto hex_length = 2 * sizeof(address{}.bytes);
    if (s.size() != 1 + hex_length || s[0] != 'Q')
        throw std::invalid_argument(
            "address must be Q followed by " + std::to_string(hex_length) + " hex characters: " + s);
    const auto addr = qrvmc::from_prefixed_hex<address>(s, "Q");
    if (!addr.has_value())
        throw std::invalid_argument("invalid address: " + s);
    return *addr;
}

/// Parses a hex string of up to 64 bytes into a hash256, left-padded; "0" and "0x0" are
/// accepted for the zero hash as exec-spec-tests write them.
template <>
hash256 from_json<hash256>(const json::json& j)
{
    // Special case to handle "0". Required by exec-spec-tests.
    // TODO: Get rid of it.
    if (j.is_string() && (j == "0" || j == "0x0"))
        return 0x00_bytes64;
    else
        return qrvmc::from_hex<hash256>(j.get<std::string>()).value();
}

/// Parses an allocation storage key as go-qrl's storageJSON (core/genesis.go) does: an
/// optional lowercase 0x prefix and at most 64 hex characters, an even number of them,
/// left-padded into 32 bytes. The key is returned in the low half of a 64-byte word, the
/// identity SLOAD, SSTORE and the state trie use, so no allocation can hold a slot the VM
/// cannot address.
static bytes64 storage_key_from_json(const json::json& j)
{
    const auto s = j.get<std::string>();
    auto hex = std::string_view{s};
    if (hex.starts_with("0x"))  // storageJSON trims only the lowercase prefix.
        hex.remove_prefix(2);
    if (hex.size() > 64)
        throw std::invalid_argument("too many hex characters in storage key: " + s);
    const auto key = qrvmc::from_hex<hash256>(hex);
    if (!key.has_value())
        throw std::invalid_argument("invalid hex storage key: " + s);
    return *key;
}

/// Parses an access-list storage key as go-qrl's common.Hash does: 0x or 0X followed by
/// exactly 64 hex characters. The prefix is normalised to 0x before decoding, which is the
/// only form the QRVMC decoder strips.
static bytes64 access_list_storage_key_from_json(const json::json& j)
{
    auto s = j.get<std::string>();
    if (s.size() != 66 || !(s.starts_with("0x") || s.starts_with("0X")))
        throw std::invalid_argument(
            "access list storage key must be 0x followed by 64 hex characters: " + s);
    s[1] = 'x';
    const auto key = qrvmc::from_hex<hash256>(s);
    if (!key.has_value())
        throw std::invalid_argument("invalid hex in access list storage key: " + s);
    return *key;
}

/// Parses a decimal or 0x hex string into a uint256; the exec-spec-tests "0x:bigint" marker
/// reads as the maximum value.
template <>
intx::uint256 from_json<intx::uint256>(const json::json& j)
{
    const auto s = j.get<std::string>();
    if (s.starts_with("0x:bigint "))
        return std::numeric_limits<intx::uint256>::max();  // Fake it
    return intx::from_string<intx::uint256>(s);
}

/// Parses an EIP-2930 access list: entries of an address and its storage keys.
template <>
state::AccessList from_json<state::AccessList>(const json::json& j)
{
    state::AccessList o;
    for (const auto& a : j)
    {
        std::vector<bytes64> storage_access_list;
        for (const auto& storage_key : a.at("storageKeys"))
            storage_access_list.emplace_back(access_list_storage_key_from_json(storage_key));
        o.emplace_back(from_json<address>(a.at("address")), std::move(storage_access_list));
    }
    return o;
}

/// Computes the EIP-1559 base fee of the current block from the parent block's gas used,
/// gas limit and base fee. Based on calculateEIP1559BaseFee from ethereum/retesteth.
inline uint64_t calculate_current_base_fee_eip1559(
    uint64_t parent_gas_used, uint64_t parent_gas_limit, uint64_t parent_base_fee)
{
    // TODO: Make sure that 64-bit precision is good enough.
    static constexpr auto BASE_FEE_MAX_CHANGE_DENOMINATOR = 8;
    static constexpr auto ELASTICITY_MULTIPLIER = 2;

    uint64_t base_fee = 0;

    const auto parent_gas_target = parent_gas_limit / ELASTICITY_MULTIPLIER;

    if (parent_gas_used == parent_gas_target)
        base_fee = parent_base_fee;
    else if (parent_gas_used > parent_gas_target)
    {
        const auto gas_used_delta = parent_gas_used - parent_gas_target;
        const auto formula =
            parent_base_fee * gas_used_delta / parent_gas_target / BASE_FEE_MAX_CHANGE_DENOMINATOR;
        const auto base_fee_per_gas_delta = formula > 1 ? formula : 1;
        base_fee = parent_base_fee + base_fee_per_gas_delta;
    }
    else
    {
        const auto gas_used_delta = parent_gas_target - parent_gas_used;

        const auto base_fee_per_gas_delta_u128 =
            intx::uint128(parent_base_fee, 0) * intx::uint128(gas_used_delta, 0) /
            parent_gas_target / BASE_FEE_MAX_CHANGE_DENOMINATOR;

        const auto base_fee_per_gas_delta = base_fee_per_gas_delta_u128[0];
        if (parent_base_fee > base_fee_per_gas_delta)
            base_fee = parent_base_fee - base_fee_per_gas_delta;
        else
            base_fee = 0;
    }
    return base_fee;
}

/// Parses the block environment ("env"): number, timestamp, gas limit, coinbase, prevrandao,
/// base fee (given, or derived from the parent block) and withdrawals.
template <>
state::BlockInfo from_json<state::BlockInfo>(const json::json& j)
{
    qrvmc::bytes64 random;
    if (const auto prev_randao_it = j.find("currentRandom"); prev_randao_it != j.end())
        random = from_json<bytes64>(*prev_randao_it);

    uint64_t base_fee = 0;
    if (j.contains("currentBaseFee"))
        base_fee = from_json<uint64_t>(j.at("currentBaseFee"));
    else if (j.contains("parentBaseFee"))
    {
        base_fee = calculate_current_base_fee_eip1559(from_json<uint64_t>(j.at("parentGasUsed")),
            from_json<uint64_t>(j.at("parentGasLimit")),
            from_json<uint64_t>(j.at("parentBaseFee")));
    }

    std::vector<state::Withdrawal> withdrawals;
    if (const auto withdrawals_it = j.find("withdrawals"); withdrawals_it != j.end())
    {
        for (const auto& withdrawal : *withdrawals_it)
        {
            withdrawals.push_back({from_json<qrvmc::address>(withdrawal.at("address")),
                from_json<uint64_t>(withdrawal.at("amount"))});
        }
    }

    return {from_json<int64_t>(j.at("currentNumber")), from_json<int64_t>(j.at("currentTimestamp")),
        from_json<int64_t>(j.at("currentGasLimit")),
        from_json<qrvmc::address>(j.at("currentCoinbase")), random, base_fee,
        std::move(withdrawals)};
}

/// Parses a state allocation: accounts keyed by address, each with nonce, balance, code
/// and storage.
template <>
state::State from_json<state::State>(const json::json& j)
{
    state::State o;
    for (const auto& [j_addr, j_acc] : j.items())
    {
        auto& acc = o.insert(from_json<address>(j_addr),
            {.nonce = from_json<uint64_t>(j_acc.at("nonce")),
                .balance = from_json<intx::uint256>(j_acc.at("balance")),
                .code = from_json<bytes>(j_acc.at("code"))});

        if (const auto storage_it = j_acc.find("storage"); storage_it != j_acc.end())
        {
            for (const auto& [j_key, j_value] : storage_it->items())
            {
                const auto value = from_json<bytes64>(j_value);
                acc.storage.insert(
                    {storage_key_from_json(j_key), {.current = value, .original = value}});
            }
        }
    }
    return o;
}

/// Translates a fork name used in test fixtures to the QRVM revision.
qrvmc_revision to_rev(std::string_view s)
{
    if (s == "Zond")
        return QRVMC_ZOND;
    throw std::invalid_argument{"unknown revision: " + std::string{s}};
}

/// Load common parts of Transaction or TestMultiTransaction.
static void from_json_tx_common(const json::json& j, state::Transaction& o)
{
    o.sender = from_json<qrvmc::address>(j.at("sender"));

    if (const auto to_it = j.find("to"); to_it != j.end() && !to_it->get<std::string>().empty())
        o.to = from_json<qrvmc::address>(*to_it);

    o.kind = state::Transaction::Kind::eip1559;
    o.max_gas_price = from_json<intx::uint256>(j.at("maxFeePerGas"));
    o.max_priority_gas_price = from_json<intx::uint256>(j.at("maxPriorityFeePerGas"));
}

/// Parses a single transaction in the t8n input form, including go-qrl's descriptor
/// and extraParams fields when present.
template <>
state::Transaction from_json<state::Transaction>(const json::json& j)
{
    state::Transaction o;
    from_json_tx_common(j, o);
    if (const auto chain_id_it = j.find("chainId"); chain_id_it != j.end())
        o.chain_id = from_json<uint8_t>(*chain_id_it);
    o.data = from_json<bytes>(j.at("input"));
    o.gas_limit = from_json<int64_t>(j.at("gas"));
    o.value = from_json<intx::uint256>(j.at("value"));

    if (const auto ac_it = j.find("accessList"); ac_it != j.end())
    {
        o.access_list = from_json<state::AccessList>(*ac_it);
    }

    if (const auto type_it = j.find("type"); type_it != j.end())
    {
        if (stdx::to_underlying(o.kind) != from_json<uint8_t>(*type_it))
            throw std::invalid_argument("wrong transaction type");
    }

    o.nonce = from_json<uint64_t>(j.at("nonce"));
    if (const auto it = j.find("descriptor"); it != j.end())
    {
        // go-qrl's DynamicFeeTx.Descriptor is a fixed 3-byte array; its JSON form may be
        // empty ("0x"), which leaves the array zero.
        const auto descriptor = from_json<bytes>(*it);
        if (!descriptor.empty() && descriptor.size() != o.descriptor.size())
            throw std::invalid_argument("descriptor must be empty or 3 bytes");
        std::copy(descriptor.begin(), descriptor.end(), o.descriptor.begin());
    }
    if (const auto it = j.find("extraParams"); it != j.end())
        o.extra_params = from_json<bytes>(*it);
    o.public_key = from_json<bytes>(j.at("publicKey"));
    o.signature = from_json<bytes>(j.at("signature"));

    return o;
}

/// Parses the "transaction" section of a state test: the common fields plus the lists of
/// inputs, access lists, gas limits and values that the post-state cases index into.
static void from_json(const json::json& j, TestMultiTransaction& o)
{
    from_json_tx_common(j, o);

    for (const auto& j_data : j.at("data"))
        o.inputs.emplace_back(from_json<bytes>(j_data));

    if (j.contains("accessLists"))
    {
        for (const auto& j_access_list : j["accessLists"])
            o.access_lists.emplace_back(from_json<state::AccessList>(j_access_list));
    }

    for (const auto& j_gas_limit : j.at("gasLimit"))
        o.gas_limits.emplace_back(from_json<int64_t>(j_gas_limit));

    for (const auto& j_value : j.at("value"))
        o.values.emplace_back(from_json<intx::uint256>(j_value));
}

/// Parses the data, gas and value indexes of a post-state case.
static void from_json(const json::json& j, TestMultiTransaction::Indexes& o)
{
    o.input = j.at("data").get<size_t>();
    o.gas_limit = j.at("gas").get<size_t>();
    o.value = j.at("value").get<size_t>();
}

/// Parses one post-state expectation: indexes, state root, logs hash and whether an
/// exception is expected.
static void from_json(const json::json& j, StateTransitionTest::Case::Expectation& o)
{
    o.indexes = j.at("indexes").get<TestMultiTransaction::Indexes>();
    o.state_hash = from_json<hash256>(j.at("hash"));
    o.logs_hash = from_json<hash256>(j.at("logs"));
    o.exception = j.contains("expectException");
}

/// Parses a whole state test: pre-state, transaction, environment, input labels and the
/// post-state cases per revision.
static void from_json(const json::json& j, StateTransitionTest& o)
{
    if (!j.is_object() || j.empty())
        throw std::invalid_argument{"JSON test must be an object with single key of the test name"};

    const auto& j_t = *j.begin();  // Content is in a dict with the test name.

    o.pre_state = from_json<state::State>(j_t.at("pre"));

    o.multi_tx = j_t.at("transaction").get<TestMultiTransaction>();

    o.block = from_json<state::BlockInfo>(j_t.at("env"));

    if (const auto& info = j_t.at("_info"); info.contains("labels"))
    {
        for (const auto& [j_id, j_label] : info.at("labels").items())
            o.input_labels.emplace(from_json<uint64_t>(j_id), j_label);
    }

    for (const auto& [rev_name, expectations] : j_t.at("post").items())
    {
        // TODO(c++20): Use emplace_back with aggregate initialization.
        o.cases.push_back({to_rev(rev_name),
            expectations.get<std::vector<StateTransitionTest::Case::Expectation>>()});
    }
}

/// Loads a state test from a JSON stream.
StateTransitionTest load_state_test(std::istream& input)
{
    return json::json::parse(input).get<StateTransitionTest>();
}
}  // namespace qrvmone::test
