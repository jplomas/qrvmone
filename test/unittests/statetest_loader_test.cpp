// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <test/statetest/statetest.hpp>
#include <test/utils/utils.hpp>

using namespace qrvmone;
using namespace qrvmone::test;
using namespace testing;

TEST(json_loader, uint64_t)
{
    using json::basic_json;

    EXPECT_EQ(from_json<uint64_t>(basic_json("0x00000005")), 5);
    EXPECT_EQ(from_json<uint64_t>(basic_json("5")), 5);
    EXPECT_EQ(from_json<uint64_t>(basic_json(7)), 7);

    EXPECT_EQ(from_json<uint64_t>(basic_json("0xffffffffffffffff")),
        std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(from_json<uint64_t>(basic_json("18446744073709551615")),
        std::numeric_limits<uint64_t>::max());
    EXPECT_THROW(from_json<uint64_t>(basic_json("0x10000000000000000")), std::out_of_range);
    EXPECT_THROW(from_json<uint64_t>(basic_json("18446744073709551616")), std::out_of_range);
    EXPECT_EQ(from_json<uint64_t>(basic_json(std::numeric_limits<uint64_t>::max())),
        std::numeric_limits<uint64_t>::max());

    // Octal is also supported.
    EXPECT_EQ(from_json<uint64_t>(basic_json("0777")), 0777);

    EXPECT_THROW(from_json<uint64_t>(basic_json("0x000000000000000k")), std::invalid_argument);
    EXPECT_THROW(from_json<uint64_t>(basic_json("k")), std::invalid_argument);
    EXPECT_THROW(from_json<uint64_t>(basic_json("")), std::invalid_argument);
}

TEST(json_loader, int64_t)
{
    using json::basic_json;

    EXPECT_EQ(from_json<int64_t>(basic_json("0x00000005")), 5);
    EXPECT_EQ(from_json<int64_t>(basic_json("-0x5")), -5);
    EXPECT_EQ(from_json<int64_t>(basic_json("-5")), -5);

    EXPECT_EQ(from_json<int64_t>(basic_json(-7)), -7);
    EXPECT_EQ(from_json<int64_t>(basic_json(0xffffffffffffffff)), -1);

    EXPECT_EQ(
        from_json<int64_t>(basic_json("0x7fffffffffffffff")), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
        from_json<int64_t>(basic_json("9223372036854775807")), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(from_json<int64_t>(basic_json("-9223372036854775808")),
        std::numeric_limits<int64_t>::min());
    EXPECT_THROW(from_json<int64_t>(basic_json("0xffffffffffffffff")), std::out_of_range);
    EXPECT_THROW(from_json<int64_t>(basic_json("9223372036854775808")), std::out_of_range);
    EXPECT_THROW(from_json<int64_t>(basic_json("-9223372036854775809")), std::out_of_range);

    // Octal is also supported.
    EXPECT_EQ(from_json<int64_t>(basic_json("0777")), 0777);

    EXPECT_THROW(from_json<int64_t>(basic_json("0x000000000000000k")), std::invalid_argument);
    EXPECT_THROW(from_json<int64_t>(basic_json("k")), std::invalid_argument);
    EXPECT_THROW(from_json<int64_t>(basic_json("")), std::invalid_argument);
}

TEST(statetest_loader, load_empty_test)
{
    std::istringstream s{"{}"};
    EXPECT_THROW(load_state_test(s), std::invalid_argument);
}

TEST(statetest_loader, load_minimal_test)
{
    std::istringstream s{R"({
        "test": {
            "_info": {},
            "pre": {},
            "transaction": {
                "sender": "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "to": "",
                "data": null,
                "gasLimit": "0",
                "value": null,
                "maxFeePerGas": "",
                "maxPriorityFeePerGas": ""
            },
            "post": {},
            "env": {
                "currentNumber": "0",
                "currentTimestamp": "0",
                "currentGasLimit": "0",
                "currentCoinbase": "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "currentRandom": ""
            }
        }
    })"};
    const StateTransitionTest st = load_state_test(s);
    // TODO: should add some comparison operator to State, BlockInfo, AccessList
    EXPECT_EQ(st.pre_state.get_accounts().size(), 0);
    EXPECT_EQ(st.block.number, 0);
    EXPECT_EQ(st.block.timestamp, 0);
    EXPECT_EQ(st.block.gas_limit, 0);
    EXPECT_EQ(st.block.coinbase, address{});
    EXPECT_EQ(st.block.prev_randao, bytes64{});
    EXPECT_EQ(st.block.base_fee, 0);
    EXPECT_EQ(st.multi_tx.kind, test::TestMultiTransaction::Kind::eip1559);
    EXPECT_EQ(st.multi_tx.data, bytes{});
    EXPECT_EQ(st.multi_tx.gas_limit, 0);
    EXPECT_EQ(st.multi_tx.max_gas_price, 0);
    EXPECT_EQ(st.multi_tx.max_priority_gas_price, 0);
    EXPECT_EQ(st.multi_tx.sender, address{});
    EXPECT_EQ(st.multi_tx.to, std::nullopt);
    EXPECT_EQ(st.multi_tx.value, 0);
    EXPECT_EQ(st.multi_tx.access_list.size(), 0);
    EXPECT_EQ(st.multi_tx.chain_id, 0);
    EXPECT_EQ(st.multi_tx.nonce, 0);
    EXPECT_EQ(st.multi_tx.public_key, bytes{});
    EXPECT_EQ(st.multi_tx.signature, bytes{});
    EXPECT_EQ(st.multi_tx.access_lists.size(), 0);
    EXPECT_EQ(st.multi_tx.inputs.size(), 0);
    EXPECT_EQ(st.multi_tx.gas_limits.size(), 1);
    EXPECT_EQ(st.multi_tx.gas_limits[0], 0);
    EXPECT_EQ(st.multi_tx.values.size(), 0);
    EXPECT_EQ(st.cases.size(), 0);
    EXPECT_EQ(st.input_labels.size(), 0);
}


TEST(statetest_loader, alloc_round_trip)
{
    using namespace qrvmc::literals;
    using namespace intx;

    // An exported allocation must load back unchanged: addresses as "Q" + 128 hex
    // characters, nonce and balance as 0x hex, code as 0x hex, non-zero storage slots.
    state::State original;
    auto& acc1 = original.insert("Q01"_address,
        {.nonce = 7, .balance = 0x0de0b6b3a7640000_u256, .code = qrvmc::bytes{0x60, 0x01, 0x00}});
    acc1.storage[0x01_bytes64] = {.current = 0xfe_bytes64, .original = 0xfe_bytes64};
    acc1.storage[0x02_bytes64] = {.current = {}, .original = 0xfd_bytes64};  // zero: left out
    // A key and a value using all 64 bytes, with non-zero high bytes, must survive unchanged.
    bytes64 wide_key{};
    wide_key.bytes[0] = 0x80;
    wide_key.bytes[63] = 0x0f;
    bytes64 wide_value{};
    wide_value.bytes[0] = 0x01;
    wide_value.bytes[63] = 0xff;
    acc1.storage[wide_key] = {.current = wide_value, .original = wide_value};
    constexpr auto addr2 =
        "Q00000000000000000000000000000000000000000000000000000000cccccccccccccccccccccccccccccccccccccccc"_address;
    original.insert(addr2, {.nonce = 0, .balance = 0, .code = {}});

    const auto j = test::export_state(original);
    ASSERT_EQ(j.size(), 2);
    EXPECT_EQ(j.at(test::hexq(addr2)).at("code"), "0x");  // empty code exports as "0x"
    for (const auto& [key, j_acc] : j.items())
    {
        EXPECT_EQ(key.size(), 129);
        EXPECT_EQ(key[0], 'Q');
        EXPECT_TRUE(j_acc.contains("nonce") && j_acc.contains("balance") && j_acc.contains("code"));
    }

    auto loaded = test::from_json<state::State>(j);
    ASSERT_EQ(loaded.get_accounts().size(), 2);

    const auto* l1 = loaded.find("Q01"_address);
    ASSERT_NE(l1, nullptr);
    EXPECT_EQ(l1->nonce, 7);
    EXPECT_EQ(l1->balance, 0x0de0b6b3a7640000_u256);
    EXPECT_EQ(l1->code, (qrvmc::bytes{0x60, 0x01, 0x00}));
    ASSERT_EQ(l1->storage.size(), 2);
    EXPECT_EQ(l1->storage.at(0x01_bytes64).current, 0xfe_bytes64);
    EXPECT_EQ(l1->storage.at(0x01_bytes64).original, 0xfe_bytes64);
    EXPECT_EQ(l1->storage.at(wide_key).current, wide_value);
    EXPECT_EQ(l1->storage.at(wide_key).original, wide_value);

    const auto* l2 = loaded.find(addr2);
    ASSERT_NE(l2, nullptr);
    EXPECT_EQ(l2->nonce, 0);
    EXPECT_EQ(l2->balance, 0);
    EXPECT_TRUE(l2->code.empty());
    EXPECT_TRUE(l2->storage.empty());
}
