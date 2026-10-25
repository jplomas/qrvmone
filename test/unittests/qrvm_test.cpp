// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "qrvm_fixture.hpp"
#include <numeric>

using namespace qrvmc::literals;
using namespace qrvmone;
using namespace intx;
using qrvmone::test::qrvm;

TEST_P(qrvm, empty)
{
    execute(0, {});
    EXPECT_GAS_USED(QRVMC_SUCCESS, 0);

    execute(1, {});
    EXPECT_GAS_USED(QRVMC_SUCCESS, 0);
}

TEST_P(qrvm, push_and_pop)
{
    execute(11, push("0102") + OP_POP + push("010203040506070809") + OP_POP);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 10);
}

TEST_P(qrvm, push_implicit_data)
{
    // This test executes 1 byte code with a push instruction without the push data following.
    // Unfortunately, there is no result we could observe other than program crash.

    // Create long bytecode prefix to force the bytecode to be stored on the heap which
    // enables invalid heap access detection via memory access validation tooling (e.g. Valgrind).
    auto code = bytecode{} + OP_PC + OP_GAS + 100 * OP_SWAP1 + OP_STOP;

    for (auto op = uint8_t{OP_PUSH1}; op < OP_PUSH1 + kMaxPushSize; ++op)
    {
        code.back() = op;
        execute(code);
        EXPECT_GAS_USED(QRVMC_SUCCESS, 307);
    }
}

TEST_P(qrvm, stack_underflow)
{
    execute(13, push(1) + OP_POP + push(1) + OP_POP + OP_POP);
    EXPECT_STATUS(QRVMC_STACK_UNDERFLOW);

    execute(bytecode{OP_NOT});
    EXPECT_STATUS(QRVMC_STACK_UNDERFLOW);
}

TEST_P(qrvm, add)
{
    execute(25, add(7, 13) + ret_top());
    EXPECT_GAS_USED(QRVMC_SUCCESS, 24);
    EXPECT_OUTPUT_INT(20);
}

TEST_P(qrvm, dup)
{
    // 0 7 3 5
    // 0 7 3 5 3 5
    // 0 7 3 5 3 5 5 7
    // 0 7 3 5 20
    // 0 7 3 5 (20 0)
    // 0 7 3 5 3 0
    const auto code = bytecode{} + push(0) + push(7) + push(3) + push(5) + OP_DUP2 + OP_DUP2 +
                      OP_DUP1 + OP_DUP6 + OP_ADD + OP_ADD + OP_ADD + OP_DUP5 + OP_MSTORE +
                      ret(32, 32);
    execute(code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 48);
    EXPECT_OUTPUT_INT(20);
}

TEST_P(qrvm, dup_all_1)
{
    auto code = push(1);
    for (auto op = uint8_t{OP_DUP1}; op <= OP_DUP16; ++op)
        code += bytecode{static_cast<Opcode>(op)};
    code += "01010101010101010101010101010101";
    execute(code + ret_top());
    EXPECT_STATUS(QRVMC_SUCCESS);
    EXPECT_OUTPUT_INT(17);
}

TEST_P(qrvm, dup_stack_overflow)
{
    auto code = push(1);
    for (auto op = uint8_t{OP_DUP1}; op <= OP_DUP16; ++op)
        code += bytecode{static_cast<Opcode>(op)};
    for (int i = 0; i < (1024 - 17); ++i)
        code += bytecode{OP_DUP16};

    execute(code);
    EXPECT_STATUS(QRVMC_SUCCESS);
    execute(code + OP_DUP16);
    EXPECT_STATUS(QRVMC_STACK_OVERFLOW);
}

TEST_P(qrvm, dup_stack_underflow)
{
    for (int i = 0; i < 16; ++i)
    {
        const auto op = static_cast<Opcode>(OP_DUP1 + i);
        execute(i * push(0) + op);
        EXPECT_STATUS(QRVMC_STACK_UNDERFLOW);
    }
}

TEST_P(qrvm, sub_and_swap)
{
    execute(push(1) + OP_DUP1 + OP_DUP2 + OP_SUB + OP_DUP1 + OP_DUP3 + OP_SWAP1 + OP_MSTORE +
            push(32) + OP_ADD + push(32) + OP_SWAP1 + OP_RETURN);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 39);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[31], 1);
}

TEST_P(qrvm, swapsn_jumpdest)
{
    // Test demonstrating possible problem with introducing multibyte SWAP/DUP instructions as per
    // EIP-663 variants B and C.
    // When SWAPSN is implemented execution will fail with QRVMC_BAD_JUMP_DESTINATION.
    const auto swapsn = "b3";
    const auto code = push(4) + OP_JUMP + swapsn + OP_JUMPDEST + push(0) + ret_top();

    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_STATUS(QRVMC_SUCCESS);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 30);

    rev = QRVMC_MAX_REVISION;
    execute(code);
    EXPECT_STATUS(QRVMC_SUCCESS);
}

TEST_P(qrvm, swapsn_push)
{
    // Test demonstrating possible problem with introducing multibyte SWAP/DUP instructions as per
    // EIP-663 variants B and C.
    // When SWAPSN is implemented execution will succeed, considering PUSH an argument of SWAPSN.
    const auto swapsn = "b3";
    const auto code = push(5) + OP_JUMP + swapsn + push(uint8_t{OP_JUMPDEST}) + push(0) + ret_top();

    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_STATUS(QRVMC_BAD_JUMP_DESTINATION);

    rev = QRVMC_MAX_REVISION;
    execute(code);
    EXPECT_STATUS(QRVMC_BAD_JUMP_DESTINATION);
}

TEST_P(qrvm, gas)
{
    execute(40, "5a5a5a010160005360016000f3");
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 13);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 38 + 36 + 34);
}

TEST_P(qrvm, arith)
{
    // x = (0 - 1) * 3
    // y = 17 s/ x
    // z = 17 s% x
    // a = 17 * x + z
    // iszero
    bytecode s;
    s += "60116001600003600302";  // 17 -3
    s += bytecode{} + OP_DUP1 + OP_DUP3 + OP_SDIV;  // 17 -3 -5
    s += bytecode{} + OP_DUP2 + OP_DUP4 + OP_SMOD;  // 17 -3 -5 2
    s += bytecode{} + OP_SWAP2 + OP_MUL + OP_ADD;   // 17 17
    s += bytecode{} + OP_SUB + OP_ISZERO;           // 1
    s += mstore8(0) + ret(0, 1);
    execute(100, s);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 26);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 1);
}

TEST_P(qrvm, comparison)
{
    bytecode s;
    s += push(0) + push(1) + OP_DUP1 + OP_DUP3 + OP_SUB + OP_DUP1 + OP_DUP1 + OP_ADD;
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_LT + mstore8(0);   // m[0] = -1 < 1
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_GT + mstore8(1);   // m[1] = -1 > 1
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_SLT + mstore8(2);  // m[2] = -1 s< 1
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_SGT + mstore8(3);  // m[3] = -1 s> 1
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_EQ + mstore8(4);   // m[4] = -1 == 1
    s += bytecode{} + OP_DUP2 + OP_DUP2 + OP_SLT + mstore8(5);  // m[5] = -2 s< -1
    s += bytecode{} + OP_DUP2 + OP_DUP2 + OP_SGT + mstore8(6);  // m[6] = -2 s> -1
    s += "60076000f3";
    execute(s);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(gas_used, 138);
    ASSERT_EQ(result.output_size, 7);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[1], 1);
    EXPECT_EQ(result.output_data[2], 1);
    EXPECT_EQ(result.output_data[3], 0);
    EXPECT_EQ(result.output_data[4], 0);
    EXPECT_EQ(result.output_data[5], 1);
    EXPECT_EQ(result.output_data[6], 0);
}

TEST_P(qrvm, bitwise)
{
    bytecode s;
    s += "60aa60ff";      // aa ff
    s += bytecode{} + OP_DUP2 + OP_DUP2 + OP_AND + mstore8(0);  // m[0] = aa & ff
    s += bytecode{} + OP_DUP2 + OP_DUP2 + OP_OR + mstore8(1);   // m[1] = aa | ff
    s += bytecode{} + OP_DUP2 + OP_DUP2 + OP_XOR + mstore8(2);  // m[2] = aa ^ ff
    s += "60036000f3";
    execute(60, s);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 3);
    EXPECT_EQ(result.output_data[0], 0xaa & 0xff);
    EXPECT_EQ(result.output_data[1], 0xaa | 0xff);
    EXPECT_EQ(result.output_data[2], 0xaa ^ 0xff);
}

TEST_P(qrvm, byte)
{
    bytecode s;
    s += "63aabbccdd";  // aabbccdd
    s += bytecode{} + OP_DUP1 + push(0) + OP_BYTE;   // DUP 0 BYTE
    s += "600053";      // m[0] = 00
    s += bytecode{} + OP_DUP1 + push(60) + OP_BYTE;  // DUP 60 BYTE
    s += "600253";      // m[2] = aa
    s += bytecode{} + OP_DUP1 + push(63) + OP_BYTE;  // DUP 63 BYTE
    s += "600453";      // m[4] = dd
    s += bytecode{} + OP_DUP1 + push(64) + OP_BYTE;  // DUP 64 BYTE
    s += "600653";      // m[6] = 00
    s += "60076000f3";  // RETURN(0,7)
    execute(72, s);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 7);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[2], 0xaa);
    EXPECT_EQ(result.output_data[4], 0xdd);
    EXPECT_EQ(result.output_data[6], 0);
}

TEST_P(qrvm, byte_overflow)
{
    const auto code = not_(0) + push(64) + OP_BYTE + ret_top();
    execute(code);
    EXPECT_OUTPUT_INT(0);

    const auto code2 = not_(0) + push("ffffffffffffffffffffffffffffffffffff") + OP_BYTE + ret_top();
    execute(code2);
    EXPECT_OUTPUT_INT(0);
}

TEST_P(qrvm, addmod_mulmod)
{
    bytecode s;
    s += "7fcdeb8272fc01d4d50a6ec165d2ea477af19b9b2c198459f59079583b97e88a66";
    s += "7f52e7e7a03b86f534d2e338aa1bb05ba3539cb2f51304cdbce69ce2d422c456ca";
    s += "7fe0f2f0cae05c220260e1724bdc66a0f83810bd1217bd105cb2da11e257c6cdf6";
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_DUP3 + OP_ADDMOD;
    s += "600052";      // m[0..]
    s += bytecode{} + OP_DUP3 + OP_DUP3 + OP_DUP3 + OP_MULMOD;
    s += "604052";      // m[64..]
    s += "60606020f3";  // RETURN(32,96)
    execute(67, s);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 96);
    EXPECT_EQ(bytes_view(&result.output_data[0], 32),
        "65ef55f81fe142622955e990252cb5209a11d4db113d842408fd9c7ae2a29a5a"_hex);
    EXPECT_EQ(bytes_view(&result.output_data[64], 32),
        "34e04890131a297202753cae4c72efd508962c9129aed8b08c8e87ab425b7258"_hex);
}

TEST_P(qrvm, divmod)
{
    // Div and mod the -1 by the input and return.
    execute(push(0) + OP_CALLDATALOAD + push(1) + push(0) + OP_SUB + OP_DUP2 + OP_DUP2 + OP_DIV +
                mstore(0) + OP_DUP2 + OP_DUP2 + OP_MOD + mstore(64) + ret(32, 96),
        "0d"_hex);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(gas_used, 61);
    ASSERT_EQ(result.output_size, 96);
    EXPECT_EQ(bytes_view(&result.output_data[0], 32),
        "0000000000000000000000000000000000000000000000000000000000000013"_hex);
    EXPECT_EQ(bytes_view(&result.output_data[64], 32),
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_hex);
}

TEST_P(qrvm, div_by_zero)
{
    execute(34, dup1(push(0)) + push(0xff) + OP_DIV + OP_SDIV + ret_top());
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_OUTPUT_INT(0);
}

TEST_P(qrvm, mod_by_zero)
{
    execute(dup1(push(0)) + push(0xeffe) + OP_MOD + OP_SMOD + ret_top());
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(gas_used, 34);
    EXPECT_OUTPUT_INT(0);
}

TEST_P(qrvm, addmod_mulmod_by_zero)
{
    execute(push(0) + OP_CALLDATALOAD + OP_DUP1 + OP_DUP1 + OP_DUP1 + OP_DUP1 + OP_ADDMOD +
            OP_MULMOD + OP_ISZERO + mstore(0) + ret(32, 32));
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(gas_used, 52);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[31], 1);
}

TEST_P(qrvm, signextend)
{
    bytecode s;
    s += "62017ffe";    // 017ffe
    s += bytecode{} + OP_DUP1 + push(0) + OP_SIGNEXTEND;
    s += "600052";      // m[0..]
    s += bytecode{} + OP_DUP1 + push(1) + OP_SIGNEXTEND;
    s += "604052";      // m[64..]
    s += "60606020f3";  // RETURN(32,96)
    execute(49, s);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 49);
    ASSERT_EQ(result.output_size, 96);
    EXPECT_EQ(hex(bytes(&result.output_data[0], 32)),
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    EXPECT_EQ(hex(bytes(&result.output_data[64], 32)),
        "0000000000000000000000000000000000000000000000000000000000007ffe");
}

TEST_P(qrvm, signextend_31)
{
    rev = QRVMC_ZOND;

    execute(bytecode{"61010160000360081c601e0b60005260206020f3"});
    EXPECT_GAS_USED(QRVMC_SUCCESS, 38);
    EXPECT_OUTPUT_INT(0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe_u256);

    execute(bytecode{"61010160000360081c601f0b60005260206020f3"});
    EXPECT_GAS_USED(QRVMC_SUCCESS, 38);
    EXPECT_OUTPUT_INT(0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe_u256);
}

TEST_P(qrvm, signextend_fuzzing)
{
    const auto signextend_reference = [](const intx::uint256& x, uint64_t ext) noexcept {
        if (ext < 31)
        {
            const auto sign_bit = ext * 8 + 7;
            const auto sign_mask = uint256{1} << sign_bit;
            const auto value_mask = sign_mask - 1;
            const auto is_neg = (x & sign_mask) != 0;
            return is_neg ? x | ~value_mask : x & value_mask;
        }
        return x;
    };

    const auto code = bytecode{} + calldataload(0) + calldataload(64) + OP_SIGNEXTEND + ret_top();

    for (int b = 0; b <= 0xff; ++b)
    {
        uint8_t input[128]{};

        auto g = b;
        for (size_t i = 0; i < 32; ++i)
            input[32 + i] = static_cast<uint8_t>(g++);  // Generate low half of SIGNEXTEND value.

        for (uint8_t e = 0; e <= 32; ++e)
        {
            input[127] = e;
            execute(code, {input, 128});
            ASSERT_EQ(output.size(), sizeof(uint256));
            const auto out = be::unsafe::load<uint256>(output.data());
            const auto expected = signextend_reference(be::unsafe::load<uint256>(&input[32]), e);
            ASSERT_EQ(out, expected);
        }
    }
}

TEST_P(qrvm, exp)
{
    const auto code = push(0x2019) + push(3) + OP_EXP + ret_top();
    execute(131, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 131);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_OUTPUT_INT(0x263cf24662b24c371a647c1340022619306e431bf3a4298d4b5998a3f1c1aaa3_u256);
}

TEST_P(qrvm, exp_1_0)
{
    const auto code = push(0) + push(1) + OP_EXP + ret_top();
    execute(31, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 31);
    EXPECT_OUTPUT_INT(1);
}

TEST_P(qrvm, exp_0_0)
{
    const auto code = push(0) + push(0) + OP_EXP + ret_top();
    execute(31, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 31);
    EXPECT_OUTPUT_INT(1);
}

TEST_P(qrvm, exp_oog)
{
    auto code = "6001600003800a";
    execute(code);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_GT(gas_used, 0);

    execute(1, code);
    EXPECT_EQ(result.status_code, QRVMC_OUT_OF_GAS);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_P(qrvm, calldataload)
{
    execute(mstore(0, calldataload(3)) + ret(0, 10), "0102030405"_hex);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 21);
    EXPECT_EQ(bytes(result.output_data, result.output_size), "04050000000000000000"_hex);
}

TEST_P(qrvm, calldataload_outofrange)
{
    execute(calldataload(1) + ret_top());
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
}

TEST_P(qrvm, address)
{
    msg.recipient.bytes[0] = 0xcc;
    const auto code = mstore(0, OP_ADDRESS) + ret(0, 10);
    execute(17, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 17);
    ASSERT_EQ(result.output_size, 10);
    EXPECT_EQ(bytes_view(&result.output_data[0], 10), "cc000000000000000000"_hex);
}

TEST_P(qrvm, caller_callvalue)
{
    msg.sender.bytes[0] = 0xdd;
    msg.value.bytes[13] = 0xee;
    const auto code = add(OP_CALLVALUE, OP_CALLER) + mstore(0) + ret(0, 16);
    execute(22, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 22);
    ASSERT_EQ(result.output_size, 16);
    EXPECT_EQ(bytes_view(&result.output_data[0], 16), "dd000000000000000000000000ee0000"_hex);
}

TEST_P(qrvm, undefined)
{
    execute(1, "2a");
    EXPECT_EQ(result.status_code, QRVMC_UNDEFINED_INSTRUCTION);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_P(qrvm, invalid)
{
    execute(1, "fe");
    EXPECT_EQ(result.status_code, QRVMC_INVALID_INSTRUCTION);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_P(qrvm, revision_out_of_range)
{
    // A host built against older QRVMC headers passes Zond as 1, which is past this VM's tables.
    rev = static_cast<qrvmc_revision>(QRVMC_MAX_REVISION + 1);
    execute(100, "00");
    EXPECT_EQ(result.status_code, QRVMC_REJECTED);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_P(qrvm, inner_stop)
{
    const auto code = push(0) + OP_STOP + OP_POP;
    execute(3, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 3);
}

TEST_P(qrvm, inner_return)
{
    const auto code = ret(0, 0) + push(0);
    execute(6, code);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 6);
}

TEST_P(qrvm, inner_revert)
{
    const auto code = revert(0, 0) + push(0);
    execute(6, code);
    EXPECT_GAS_USED(QRVMC_REVERT, 6);
}

TEST_P(qrvm, inner_invalid)
{
    const auto code = push(0) + "fe" + OP_POP;
    execute(5, code);
    EXPECT_GAS_USED(QRVMC_INVALID_INSTRUCTION, 5);
}

TEST_P(qrvm, keccak256)
{
    execute(push(0x0800) + push(0x03ff) + OP_KECCAK256 + ret_top());
    EXPECT_GAS_USED(QRVMC_SUCCESS, 388);
    EXPECT_OUTPUT_INT(0xaeffb38c06e111d84216396baefeb7fed397f303d5cb84a33f1e8b485c4a22da_u256);
}

TEST_P(qrvm, keccak256_empty)
{
    auto code = push(0) + OP_DUP1 + OP_KECCAK256 + ret_top();
    execute(code);
    ASSERT_EQ(result.output_size, 32);
    auto keccak256_empty = "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
    EXPECT_EQ(hex({result.output_data, result.output_size}), keccak256_empty);
}

TEST_P(qrvm, revert)
{
    bytecode s;
    s += push(0xee) + OP_DUP1 + OP_MSTORE8;  // m[ee] == ee
    s += "600260edfd";  // REVERT(ee,1)
    execute(s);
    EXPECT_EQ(gas_used, 27);
    EXPECT_EQ(result.status_code, QRVMC_REVERT);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[1], 0xee);
}

TEST_P(qrvm, return_empty_buffer_at_offset_0)
{
    execute(dup1(OP_MSIZE) + OP_RETURN);
    EXPECT_GAS_USED(QRVMC_SUCCESS, 5);
}

TEST_P(qrvm, return_empty_buffer_at_high_offset)
{
    host.tx_context.block_prev_randao =
        0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1_bytes64;

    execute(push(0) + OP_PREVRANDAO + OP_RETURN);
    EXPECT_STATUS(QRVMC_SUCCESS);

    execute(push(0) + OP_PREVRANDAO + OP_REVERT);
    EXPECT_STATUS(QRVMC_REVERT);
}

TEST_P(qrvm, shl)
{
    const bytecode code = "600560011b6000526001603ff3";
    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 5 << 1);
}

TEST_P(qrvm, shr)
{
    const bytecode code = "600560011c6000526001603ff3";
    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 5 >> 1);
}

TEST_P(qrvm, sar)
{
    const bytecode code = "600160000360021d60005260016000f3";
    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_EQ(gas_used, 30);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0xff);  // MSB of (-1 >> 2) == -1
}

TEST_P(qrvm, sar_01)
{
    const bytecode code = "600060011d60005260016000f3";
    rev = QRVMC_ZOND;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_P(qrvm, shift_overflow)
{
    rev = QRVMC_ZOND;
    for (auto op : {OP_SHL, OP_SHR, OP_SAR})
    {
        execute(not_(0) + 0x200 + op + ret_top());
        EXPECT_EQ(result.status_code, QRVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 32);
        auto a = std::accumulate(result.output_data, result.output_data + result.output_size, 0);
        EXPECT_EQ(a, op == OP_SAR ? 32 * 0xff : 0);
    }
}

TEST_P(qrvm, undefined_instructions)
{
    for (auto i = 0; i <= QRVMC_MAX_REVISION; ++i)
    {
        const auto r = qrvmc_revision(i);
        for (uint8_t opcode = 0; opcode <= 0xfe; ++opcode)
        {
            if (qrvmone::instr::gas_costs[r][opcode] != qrvmone::instr::undefined)
                continue;

            auto res = vm.execute(host, r, {}, &opcode, sizeof(opcode));
            EXPECT_EQ(res.status_code, QRVMC_UNDEFINED_INSTRUCTION)
                << " for opcode " << hex(opcode) << " on revision " << r;
        }
    }
}

TEST_P(qrvm, undefined_instruction_analysis_overflow)
{
    rev = QRVMC_ZOND;

    auto undefined_opcode = static_cast<Opcode>(0x0c);
    auto code = bytecode{undefined_opcode};

    execute(code);
    EXPECT_EQ(result.status_code, QRVMC_UNDEFINED_INSTRUCTION);
}

TEST_P(qrvm, undefined_instruction_block_cost_negative)
{
    // For undefined instructions QRVMC instruction tables have cost -1.
    // If naively counted block costs can become negative.

    const auto max_gas = std::numeric_limits<int64_t>::max();

    const auto code1 = bytecode{} + "0f";  // Block cost -1.
    execute(max_gas, code1);
    EXPECT_STATUS(QRVMC_UNDEFINED_INSTRUCTION);

    const auto code2 = bytecode{} + OP_JUMPDEST + "c6" + "4b" + OP_STOP;  // Block cost -1.
    execute(max_gas, code2);
    EXPECT_STATUS(QRVMC_UNDEFINED_INSTRUCTION);

    const auto code3 = bytecode{} + OP_ADDRESS + "2a" + "2b" + "2c" + "2d";  // Block cost -2.
    execute(max_gas, code3);
    EXPECT_STATUS(QRVMC_UNDEFINED_INSTRUCTION);
}

TEST_P(qrvm, abort)
{
    for (auto r = 11; r <= QRVMC_MAX_REVISION; ++r)
    {
        auto opcode = uint8_t{0xfe};
        auto res = vm.execute(host, qrvmc_revision(r), {}, &opcode, sizeof(opcode));
        EXPECT_EQ(res.status_code, QRVMC_INVALID_INSTRUCTION);
    }
}

TEST_P(qrvm, staticmode)
{
    auto code_prefix = 1 + 6 * OP_DUP1;

    rev = QRVMC_ZOND;
    for (auto op :
        {OP_SSTORE, OP_LOG0, OP_LOG1, OP_LOG2, OP_LOG3, OP_LOG4, OP_CALL, OP_CREATE, OP_CREATE2})
    {
        msg.flags |= QRVMC_STATIC;
        execute(code_prefix + hex(op));
        EXPECT_EQ(result.status_code, QRVMC_STATIC_MODE_VIOLATION) << hex(op);
        EXPECT_EQ(result.gas_left, 0);
    }
}

TEST_P(qrvm, max_code_size_push1)
{
    constexpr auto max_code_size = 0x6000;
    const auto code = (max_code_size / 2) * push(1);
    ASSERT_EQ(code.size(), max_code_size);

    execute(code);
    EXPECT_STATUS(QRVMC_STACK_OVERFLOW);

    execute({code.data(), code.size() - 1});
    EXPECT_STATUS(QRVMC_STACK_OVERFLOW);
}

TEST_P(qrvm, reverse_16_stack_items)
{
    // This test puts values 1, 2, ... , 16 on the stack and then reverse them with SWAP opcodes.
    // This uses all variants of SWAP instruction.

    constexpr auto n = 16;
    auto code = bytecode{};
    for (uint64_t i = 1; i <= n; ++i)
        code += push(i);
    code += push(0);                                        // Temporary stack item.
    code += bytecode{} + OP_SWAP16 + OP_SWAP1 + OP_SWAP16;  // Swap 1 and 16.
    code += bytecode{} + OP_SWAP15 + OP_SWAP2 + OP_SWAP15;  // Swap 2 and 15.
    code += bytecode{} + OP_SWAP14 + OP_SWAP3 + OP_SWAP14;
    code += bytecode{} + OP_SWAP13 + OP_SWAP4 + OP_SWAP13;
    code += bytecode{} + OP_SWAP12 + OP_SWAP5 + OP_SWAP12;
    code += bytecode{} + OP_SWAP11 + OP_SWAP6 + OP_SWAP11;
    code += bytecode{} + OP_SWAP10 + OP_SWAP7 + OP_SWAP10;
    code += bytecode{} + OP_SWAP9 + OP_SWAP8 + OP_SWAP9;
    code += bytecode{} + OP_POP;
    for (uint64_t i = 0; i < n; ++i)
        code += mstore8(i);
    code += ret(0, n);

    execute(code);

    EXPECT_STATUS(QRVMC_SUCCESS);
    ASSERT_EQ(result.output_size, n);
    EXPECT_EQ(hex({result.output_data, result.output_size}), "0102030405060708090a0b0c0d0e0f10");
}
