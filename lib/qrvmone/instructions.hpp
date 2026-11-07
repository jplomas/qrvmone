// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "baseline.hpp"
#include "execution_state.hpp"
#include "instructions_traits.hpp"
#include "instructions_xmacro.hpp"
#include <ethash/keccak.hpp>

namespace qrvmone
{

/// Generic addmod for any uint<N> (intx only provides uint256 specialization)
template <unsigned N>
inline constexpr intx::uint<N> addmod(const intx::uint<N>& x, const intx::uint<N>& y, const intx::uint<N>& mod) noexcept
{
    auto s = intx::addc(x, y);
    intx::uint<N + 64> n = s.value;
    n[intx::uint<N>::num_words] = s.carry;
    return intx::udivrem(n, mod).rem;
}

/// Generic mulmod for any uint<N>
template <unsigned N>
inline constexpr intx::uint<N> mulmod(const intx::uint<N>& x, const intx::uint<N>& y, const intx::uint<N>& mod) noexcept
{
    return intx::udivrem(intx::umul(x, y), mod).rem;
}

/// Special case: load from qrvmc_address (64 bytes) as uint512.
inline uint512 load_uint_addr(const qrvmc_address& src) noexcept
{
    uint512 result = 0;
    for (size_t i = 0; i < sizeof(src.bytes); ++i)
        result = (result << 8) | src.bytes[i];
    return result;
}

/// Store uint512 into qrvmc_address, taking all 64 bytes.
inline qrvmc_address trunc_to_addr(const uint512& x) noexcept
{
    qrvmc_address addr{};
    uint512 tmp = x;
    for (int i = static_cast<int>(sizeof(addr.bytes)) - 1; i >= 0; --i)
    {
        addr.bytes[i] = static_cast<uint8_t>(tmp);
        tmp >>= 8;
    }
    return addr;
}

/// Store right-aligned uint512 value into qrvmc_bytes64 (64 bytes, big-endian).
/// Lower 256 bits go into bytes 32-63. Upper 256 bits into bytes 0-31.
inline qrvmc::bytes64 store_uint(const uint512& x) noexcept
{
    qrvmc::bytes64 result{};
    uint512 tmp = x;
    for (int i = 63; i >= 0; --i)
    {
        result.bytes[i] = static_cast<uint8_t>(tmp);
        tmp >>= 8;
    }
    return result;
}

/// Store the storage key addressed by a 64-byte stack operand.
/// Storage keys are 32 bytes: SLOAD and SSTORE key storage by the low 256 bits
/// of the operand, matching go-qrl. The key is still passed to the host as
/// qrvmc_bytes64, with the upper 32 bytes zeroed, so operands that differ only
/// above bit 255 address the same slot.
inline qrvmc::bytes64 store_storage_key(const uint512& x) noexcept
{
    auto key = store_uint(x);
    for (size_t i = 0; i < 32; ++i)
        key.bytes[i] = 0;
    return key;
}

using code_iterator = const uint8_t*;

/// Represents the pointer to the stack top item
/// and allows retrieving stack items and manipulating the pointer.
class StackTop
{
    uint512* m_top;

public:
    StackTop(uint512* top) noexcept : m_top{top} {}

    /// Returns the reference to the stack item by index, where 0 means the top item
    /// and positive index values the items further down the stack.
    /// Using [-1] is also valid, but .push() should be used instead.
    [[nodiscard]] uint512& operator[](int index) noexcept { return m_top[-index]; }

    /// Returns the reference to the stack top item.
    [[nodiscard]] uint512& top() noexcept { return *m_top; }

    /// Returns the current top item and move the stack top pointer down.
    /// The value is returned by reference because the stack slot remains valid.
    [[nodiscard]] uint512& pop() noexcept { return *m_top--; }

    /// Assigns the value to the stack top and moves the stack top pointer up.
    void push(const uint512& value) noexcept { *++m_top = value; }
};


/// Instruction execution result.
struct Result
{
    qrvmc_status_code status;
    int64_t gas_left;
};

/// Instruction result indicating that execution terminates unconditionally.
struct TermResult : Result
{};

constexpr auto max_buffer_size = std::numeric_limits<uint32_t>::max();

/// The size of the QRVM 512-bit word.
constexpr auto word_size = 64;

/// Returns number of words what would fit to provided number of bytes,
/// i.e. it rounds up the number bytes to number of words.
inline constexpr int64_t num_words(uint64_t size_in_bytes) noexcept
{
    return static_cast<int64_t>((size_in_bytes + (word_size - 1)) / word_size);
}

/// Grows QRVM memory and checks its cost.
///
/// This function should not be inlined because this may affect other inlining decisions:
/// - making check_memory() too costly to inline,
/// - making mload()/mstore()/mstore8() too costly to inline.
///
/// TODO: This function should be moved to Memory class.
[[gnu::noinline]] inline int64_t grow_memory(
    int64_t gas_left, Memory& memory, uint64_t new_size) noexcept
{
    // This implementation recomputes memory.size(). This value is already known to the caller
    // and can be passed as a parameter, but this make no difference to the performance.

    const auto new_words = num_words(new_size);
    const auto current_words = static_cast<int64_t>(memory.size() / word_size);
    const auto new_cost = 3 * new_words + new_words * new_words / 512;
    const auto current_cost = 3 * current_words + current_words * current_words / 512;
    const auto cost = new_cost - current_cost;

    gas_left -= cost;
    if (gas_left >= 0) [[likely]]
        memory.grow(static_cast<size_t>(new_words * word_size));
    return gas_left;
}

/// Check memory requirements of a reasonable size.
inline bool check_memory(
    int64_t& gas_left, Memory& memory, const uint512& offset, uint64_t size) noexcept
{
    // TODO: This should be done in intx.
    // There is "branchless" variant of this using | instead of ||, but benchmarks difference
    // is within noise. This should be decided when moving the implementation to intx.
    if (((offset[7] | offset[6] | offset[5] | offset[4] | offset[3] | offset[2] | offset[1]) != 0) || (offset[0] > max_buffer_size))
        return false;

    const auto new_size = static_cast<uint64_t>(offset) + size;
    if (new_size > memory.size())
        gas_left = grow_memory(gas_left, memory, new_size);

    return gas_left >= 0;  // Always true for no-grow case.
}

/// Check memory requirements for "copy" instructions.
inline bool check_memory(
    int64_t& gas_left, Memory& memory, const uint512& offset, const uint512& size) noexcept
{
    if (size == 0)  // Copy of size 0 is always valid (even if offset is huge).
        return true;

    // This check has 7 same word checks with the check above.
    // However, compilers do decent although not perfect job unifying common instructions.
    // TODO: This should be done in intx.
    if (((size[7] | size[6] | size[5] | size[4] | size[3] | size[2] | size[1]) != 0) || (size[0] > max_buffer_size))
        return false;

    return check_memory(gas_left, memory, offset, static_cast<uint64_t>(size));
}

namespace instr::core
{

/// The "core" instruction implementations.
///
/// These are minimal QRVM instruction implementations which assume:
/// - the stack requirements (overflow, underflow) have already been checked,
/// - the "base" gas const has already been charged,
/// - the `stack` pointer points to the QRVM stack top element.
/// Moreover, these implementations _do not_ inform about new stack height
/// after execution. The adjustment must be performed by the caller.
inline void noop(StackTop /*stack*/) noexcept {}
inline constexpr auto pop = noop;
inline constexpr auto jumpdest = noop;

template <qrvmc_status_code Status>
inline TermResult stop_impl(
    StackTop /*stack*/, int64_t gas_left, ExecutionState& /*state*/) noexcept
{
    return {Status, gas_left};
}
inline constexpr auto stop = stop_impl<QRVMC_SUCCESS>;
inline constexpr auto invalid = stop_impl<QRVMC_INVALID_INSTRUCTION>;

inline void add(StackTop stack) noexcept
{
    stack.top() += stack.pop();
}

inline void mul(StackTop stack) noexcept
{
    stack.top() *= stack.pop();
}

inline void sub(StackTop stack) noexcept
{
    stack[1] = stack[0] - stack[1];
}

inline void div(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? stack[0] / v : 0;
}

inline void sdiv(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? intx::sdivrem(stack[0], v).quot : 0;
}

inline void mod(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? stack[0] % v : 0;
}

inline void smod(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? intx::sdivrem(stack[0], v).rem : 0;
}

inline void addmod(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    const auto& y = stack.pop();
    auto& m = stack.top();
    m = m != 0 ? qrvmone::addmod(x, y, m) : 0;
}

inline void mulmod(StackTop stack) noexcept
{
    const auto& x = stack[0];
    const auto& y = stack[1];
    auto& m = stack[2];
    m = m != 0 ? qrvmone::mulmod(x, y, m) : 0;
}

inline Result exp(StackTop stack, int64_t gas_left, ExecutionState& /*state*/) noexcept
{
    const auto& base = stack.pop();
    auto& exponent = stack.top();

    const auto exponent_significant_bytes =
        static_cast<int>(intx::count_significant_bytes(exponent));
    const auto exponent_cost = 50;
    const auto additional_cost = exponent_significant_bytes * exponent_cost;
    if ((gas_left -= additional_cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    exponent = intx::exp(base, exponent);
    return {QRVMC_SUCCESS, gas_left};
}

inline void signextend(StackTop stack) noexcept
{
    const auto& ext = stack.pop();
    auto& x = stack.top();

    if (ext < 63)  // For 63 we also don't need to do anything.
    {
        const auto e = ext[0];  // uint512 -> uint64 (the low 64-bit limb).
        const auto sign_word_index =
            static_cast<size_t>(e / sizeof(e));      // Index of the word with the sign bit.
        const auto sign_byte_index = e % sizeof(e);  // Index of the sign byte in the sign word.
        auto& sign_word = x[sign_word_index];

        const auto sign_byte_offset = sign_byte_index * 8;
        const auto sign_byte = sign_word >> sign_byte_offset;  // Move sign byte to position 0.

        // Sign-extend the "sign" byte and move it to the right position. Value bits are zeros.
        const auto sext_byte = static_cast<uint64_t>(int64_t{static_cast<int8_t>(sign_byte)});
        const auto sext = sext_byte << sign_byte_offset;

        const auto sign_mask = ~uint64_t{0} << sign_byte_offset;
        const auto value = sign_word & ~sign_mask;  // Reset extended bytes.
        sign_word = sext | value;                   // Combine the result word.

        // Produce bits (all zeros or ones) for extended words. This is done by SAR of
        // the sign-extended byte. Shift by any value 7-63 would work.
        const auto sign_ex = static_cast<uint64_t>(static_cast<int64_t>(sext_byte) >> 8);

        for (size_t i = 7; i > sign_word_index; --i)
            x[i] = sign_ex;  // Clear extended words.
    }
}

inline void lt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = x < stack[0];
}

inline void gt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = stack[0] < x;  // Arguments are swapped and < is used.
}

inline void slt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = slt(x, stack[0]);
}

inline void sgt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = slt(stack[0], x);  // Arguments are swapped and SLT is used.
}

inline void eq(StackTop stack) noexcept
{
    stack[1] = stack[0] == stack[1];
}

inline void iszero(StackTop stack) noexcept
{
    stack.top() = stack.top() == 0;
}

inline void and_(StackTop stack) noexcept
{
    stack.top() &= stack.pop();
}

inline void or_(StackTop stack) noexcept
{
    stack.top() |= stack.pop();
}

inline void xor_(StackTop stack) noexcept
{
    stack.top() ^= stack.pop();
}

inline void not_(StackTop stack) noexcept
{
    stack.top() = ~stack.top();
}

inline void byte(StackTop stack) noexcept
{
    const auto& n = stack.pop();
    auto& x = stack.top();

    const bool n_valid = n < 64;
    const uint64_t byte_mask = (n_valid ? 0xff : 0);

    const auto index = 63 - static_cast<unsigned>(n[0] % 64);
    const auto word = x[index / 8];
    const auto byte_index = index % 8;
    const auto byte = (word >> (byte_index * 8)) & byte_mask;
    x = byte;
}

inline void shl(StackTop stack) noexcept
{
    stack.top() <<= stack.pop();
}

inline void shr(StackTop stack) noexcept
{
    stack.top() >>= stack.pop();
}

inline void sar(StackTop stack) noexcept
{
    const auto& y = stack.pop();
    auto& x = stack.top();

    const bool is_neg = static_cast<int64_t>(x[7]) < 0;  // Inspect the top bit (words are LE).
    const auto sign_mask = is_neg ? ~uint512{} : uint512{};

    const auto mask_shift = (y < 512) ? (512 - y[0]) : 0;
    x = (x >> y) | (sign_mask << mask_shift);
}

inline Result keccak256(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    auto& size = stack.top();

    if (!check_memory(gas_left, state.memory, index, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    const auto i = static_cast<size_t>(index);
    const auto s = static_cast<size_t>(size);
    const auto w = num_words(s);
    const auto cost = w * 6;
    if ((gas_left -= cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    auto data = s != 0 ? &state.memory[i] : nullptr;
    // KECCAK256 is an opcode-level 256-bit hash result. Keep it right-aligned
    // in the 512-bit stack word; high-level fixed-bytes codegen left-aligns it
    // when the Hyperion type is bytes32.
    size = uint512(intx::be::load<uint256>(ethash::keccak256(data, s)));
    return {QRVMC_SUCCESS, gas_left};
}


inline void address(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(load_uint_addr(state.msg->recipient));
}

inline Result balance(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = trunc_to_addr(x);

    if (state.host.access_account(addr) == QRVMC_ACCESS_COLD)
    {
        if ((gas_left -= instr::additional_cold_account_access_cost) < 0)
            return {QRVMC_OUT_OF_GAS, gas_left};
    }

    x = intx::be::load<uint512>(state.host.get_balance(addr));
    return {QRVMC_SUCCESS, gas_left};
}

inline void origin(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(load_uint_addr(state.get_tx_context().tx_origin));
}

inline void caller(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(load_uint_addr(state.msg->sender));
}

inline void callvalue(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint512>(state.msg->value));
}

inline void calldataload(StackTop stack, ExecutionState& state) noexcept
{
    auto& index = stack.top();

    if (state.msg->input_size < index)
        index = 0;
    else
    {
        const auto begin = static_cast<size_t>(index);
        const auto end = std::min(begin + 64, state.msg->input_size);

        uint8_t data[64] = {};
        for (size_t i = 0; i < (end - begin); ++i)
            data[i] = state.msg->input_data[begin + i];

        index = intx::be::load<uint512>(data);
    }
}

inline void calldatasize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.msg->input_size);
}

inline Result calldatacopy(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(gas_left, state.memory, mem_index, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    auto dst = static_cast<size_t>(mem_index);
    auto src = state.msg->input_size < input_index ? state.msg->input_size :
                                                     static_cast<size_t>(input_index);
    auto s = static_cast<size_t>(size);
    auto copy_size = std::min(s, state.msg->input_size - src);

    const auto copy_cost = num_words(s) * 3;
    if ((gas_left -= copy_cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    if (copy_size > 0)
        std::memcpy(&state.memory[dst], &state.msg->input_data[src], copy_size);

    if (s - copy_size > 0)
        std::memset(&state.memory[dst + copy_size], 0, s - copy_size);

    return {QRVMC_SUCCESS, gas_left};
}

inline void codesize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.original_code.size());
}

inline Result codecopy(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    // TODO: Similar to calldatacopy().

    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(gas_left, state.memory, mem_index, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    const auto code_size = state.original_code.size();
    const auto dst = static_cast<size_t>(mem_index);
    const auto src = code_size < input_index ? code_size : static_cast<size_t>(input_index);
    const auto s = static_cast<size_t>(size);
    const auto copy_size = std::min(s, code_size - src);

    const auto copy_cost = num_words(s) * 3;
    if ((gas_left -= copy_cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    // TODO: Add unit tests for each combination of conditions.
    if (copy_size > 0)
        std::memcpy(&state.memory[dst], &state.original_code[src], copy_size);

    if (s - copy_size > 0)
        std::memset(&state.memory[dst + copy_size], 0, s - copy_size);

    return {QRVMC_SUCCESS, gas_left};
}


inline void gasprice(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint512>(state.get_tx_context().tx_gas_price));
}

inline void basefee(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint512>(state.get_tx_context().block_base_fee));
}

inline Result extcodesize(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = trunc_to_addr(x);

    if (state.host.access_account(addr) == QRVMC_ACCESS_COLD)
    {
        if ((gas_left -= instr::additional_cold_account_access_cost) < 0)
            return {QRVMC_OUT_OF_GAS, gas_left};
    }

    x = state.host.get_code_size(addr);
    return {QRVMC_SUCCESS, gas_left};
}

inline Result extcodecopy(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto addr = trunc_to_addr(stack.pop());
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(gas_left, state.memory, mem_index, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    const auto s = static_cast<size_t>(size);
    const auto copy_cost = num_words(s) * 3;
    if ((gas_left -= copy_cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    if (state.host.access_account(addr) == QRVMC_ACCESS_COLD)
    {
        if ((gas_left -= instr::additional_cold_account_access_cost) < 0)
            return {QRVMC_OUT_OF_GAS, gas_left};
    }

    if (s > 0)
    {
        const auto src =
            (max_buffer_size < input_index) ? max_buffer_size : static_cast<size_t>(input_index);
        const auto dst = static_cast<size_t>(mem_index);
        const auto num_bytes_copied = state.host.copy_code(addr, src, &state.memory[dst], s);
        if (const auto num_bytes_to_clear = s - num_bytes_copied; num_bytes_to_clear > 0)
            std::memset(&state.memory[dst + num_bytes_copied], 0, num_bytes_to_clear);
    }

    return {QRVMC_SUCCESS, gas_left};
}

inline void returndatasize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.return_data.size());
}

inline Result returndatacopy(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(gas_left, state.memory, mem_index, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    auto dst = static_cast<size_t>(mem_index);
    auto s = static_cast<size_t>(size);

    if (state.return_data.size() < input_index)
        return {QRVMC_INVALID_MEMORY_ACCESS, gas_left};
    auto src = static_cast<size_t>(input_index);

    if (src + s > state.return_data.size())
        return {QRVMC_INVALID_MEMORY_ACCESS, gas_left};

    const auto copy_cost = num_words(s) * 3;
    if ((gas_left -= copy_cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    if (s > 0)
        std::memcpy(&state.memory[dst], &state.return_data[src], s);

    return {QRVMC_SUCCESS, gas_left};
}

inline Result extcodehash(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = trunc_to_addr(x);

    if (state.host.access_account(addr) == QRVMC_ACCESS_COLD)
    {
        if ((gas_left -= instr::additional_cold_account_access_cost) < 0)
            return {QRVMC_OUT_OF_GAS, gas_left};
    }

    x = intx::be::load<uint512>(state.host.get_code_hash(addr));
    return {QRVMC_SUCCESS, gas_left};
}


inline void blockhash(StackTop stack, ExecutionState& state) noexcept
{
    auto& number = stack.top();

    const auto upper_bound = state.get_tx_context().block_number;
    const auto lower_bound = std::max(upper_bound - 256, decltype(upper_bound){0});
    const auto n = static_cast<int64_t>(number);
    const auto header =
        (number < upper_bound && n >= lower_bound) ? state.host.get_block_hash(n) : qrvmc::bytes64{};
    number = intx::be::load<uint512>(header);
}

inline void coinbase(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(load_uint_addr(state.get_tx_context().block_coinbase));
}

inline void timestamp(StackTop stack, ExecutionState& state) noexcept
{
    // TODO: Add tests for negative timestamp?
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_timestamp));
}

inline void number(StackTop stack, ExecutionState& state) noexcept
{
    // TODO: Add tests for negative block number?
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_number));
}

inline void prevrandao(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint512>(state.get_tx_context().block_prev_randao));
}

inline void gaslimit(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_gas_limit));
}

inline void chainid(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint512>(state.get_tx_context().chain_id));
}

inline void selfbalance(StackTop stack, ExecutionState& state) noexcept
{
    // TODO: introduce selfbalance in QRVMC?
    stack.push(intx::be::load<uint512>(state.host.get_balance(state.msg->recipient)));
}

inline Result mload(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    auto& index = stack.top();

    if (!check_memory(gas_left, state.memory, index, 64))
        return {QRVMC_OUT_OF_GAS, gas_left};

    index = intx::be::unsafe::load<uint512>(&state.memory[static_cast<size_t>(index)]);
    return {QRVMC_SUCCESS, gas_left};
}

inline Result mstore(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    const auto& value = stack.pop();

    if (!check_memory(gas_left, state.memory, index, 64))
        return {QRVMC_OUT_OF_GAS, gas_left};

    intx::be::unsafe::store(&state.memory[static_cast<size_t>(index)], value);
    return {QRVMC_SUCCESS, gas_left};
}

inline Result mstore8(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    const auto& value = stack.pop();

    if (!check_memory(gas_left, state.memory, index, 1))
        return {QRVMC_OUT_OF_GAS, gas_left};

    state.memory[static_cast<size_t>(index)] = static_cast<uint8_t>(value);
    return {QRVMC_SUCCESS, gas_left};
}

Result sload(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept;

Result sstore(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept;

/// Internal jump implementation for JUMP/JUMPI instructions.
inline code_iterator jump_impl(ExecutionState& state, const uint512& dst) noexcept
{
    const auto& jumpdest_map = state.analysis.baseline->jumpdest_map;
    if (dst >= jumpdest_map.size() || !jumpdest_map[static_cast<size_t>(dst)])
    {
        state.status = QRVMC_BAD_JUMP_DESTINATION;
        return nullptr;
    }

    return &state.analysis.baseline->executable_code[static_cast<size_t>(dst)];
}

/// JUMP instruction implementation using baseline::CodeAnalysis.
inline code_iterator jump(StackTop stack, ExecutionState& state, code_iterator /*pos*/) noexcept
{
    return jump_impl(state, stack.pop());
}

/// JUMPI instruction implementation using baseline::CodeAnalysis.
inline code_iterator jumpi(StackTop stack, ExecutionState& state, code_iterator pos) noexcept
{
    const auto& dst = stack.pop();
    const auto& cond = stack.pop();
    return cond ? jump_impl(state, dst) : pos + 1;
}

inline code_iterator pc(StackTop stack, ExecutionState& state, code_iterator pos) noexcept
{
    stack.push(static_cast<uint64_t>(pos - state.analysis.baseline->executable_code.data()));
    return pos + 1;
}

inline void msize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.memory.size());
}

inline Result gas(StackTop stack, int64_t gas_left, ExecutionState& /*state*/) noexcept
{
    stack.push(gas_left);
    return {QRVMC_SUCCESS, gas_left};
}

inline void push0(StackTop stack) noexcept
{
    stack.push({});
}


template <size_t Len>
inline uint64_t load_partial_push_data(code_iterator pos) noexcept
{
    static_assert(Len > 4 && Len < 8);

    // It loads up to 3 additional bytes.
    return intx::be::unsafe::load<uint64_t>(pos) >> (8 * (sizeof(uint64_t) - Len));
}

template <>
inline uint64_t load_partial_push_data<1>(code_iterator pos) noexcept
{
    return pos[0];
}

template <>
inline uint64_t load_partial_push_data<2>(code_iterator pos) noexcept
{
    return intx::be::unsafe::load<uint16_t>(pos);
}

template <>
inline uint64_t load_partial_push_data<3>(code_iterator pos) noexcept
{
    // It loads 1 additional byte.
    return intx::be::unsafe::load<uint32_t>(pos) >> 8;
}

template <>
inline uint64_t load_partial_push_data<4>(code_iterator pos) noexcept
{
    return intx::be::unsafe::load<uint32_t>(pos);
}

/// PUSH instruction implementation.
/// @tparam Len The number of push data bytes, e.g. PUSH3 is push<3>.
///
/// It assumes that at lest 32 bytes of data are available so code padding is required.
template <size_t Len>
inline code_iterator push(StackTop stack, ExecutionState& /*state*/, code_iterator pos) noexcept
{
    constexpr auto num_full_words = Len / sizeof(uint64_t);
    constexpr auto num_partial_bytes = Len % sizeof(uint64_t);
    auto data = pos + 1;

    stack.push(0);
    auto& r = stack.top();

    // Load top partial word.
    if constexpr (num_partial_bytes != 0)
    {
        r[num_full_words] = load_partial_push_data<num_partial_bytes>(data);
        data += num_partial_bytes;
    }

    // Load full words.
    for (size_t i = 0; i < num_full_words; ++i)
    {
        r[num_full_words - 1 - i] = intx::be::unsafe::load<uint64_t>(data);
        data += sizeof(uint64_t);
    }

    return pos + (Len + 1);
}

/// DUP instruction implementation.
/// @tparam N  The number as in the instruction definition, e.g. DUP3 is dup<3>.
template <int N>
inline void dup(StackTop stack) noexcept
{
    static_assert(N >= 1 && N <= 16);
    stack.push(stack[N - 1]);
}

/// SWAP instruction implementation.
/// @tparam N  The number as in the instruction definition, e.g. SWAP3 is swap<3>.
template <int N>
inline void swap(StackTop stack) noexcept
{
    static_assert(N >= 1 && N <= 16);

    // The simple std::swap(stack.top(), stack[N]) is not used to workaround
    // clang missed optimization: https://github.com/llvm/llvm-project/issues/59116
    // TODO(clang): Check if #59116 bug fix has been released.

    auto& a = stack[N];
    auto& t = stack.top();
    auto t0 = t[0];
    auto t1 = t[1];
    auto t2 = t[2];
    auto t3 = t[3];
    auto t4 = t[4];
    auto t5 = t[5];
    auto t6 = t[6];
    auto t7 = t[7];
    t = a;
    a[0] = t0;
    a[1] = t1;
    a[2] = t2;
    a[3] = t3;
    a[4] = t4;
    a[5] = t5;
    a[6] = t6;
    a[7] = t7;
}

template <size_t NumTopics>
inline Result log(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    static_assert(NumTopics <= 4);

    if (state.in_static_mode())
        return {QRVMC_STATIC_MODE_VIOLATION, 0};

    const auto& offset = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(gas_left, state.memory, offset, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    const auto o = static_cast<size_t>(offset);
    const auto s = static_cast<size_t>(size);

    const auto cost = int64_t(s) * 8;
    if ((gas_left -= cost) < 0)
        return {QRVMC_OUT_OF_GAS, gas_left};

    std::array<qrvmc::bytes64, NumTopics> topics;  // NOLINT(cppcoreguidelines-pro-type-member-init)
    for (auto& topic : topics)
        topic = intx::be::store<qrvmc::bytes64>(stack.pop());

    const auto data = s != 0 ? &state.memory[o] : nullptr;
    state.host.emit_log(state.msg->recipient, data, s, topics.data(), NumTopics);
    return {QRVMC_SUCCESS, gas_left};
}


template <Opcode Op>
Result call_impl(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept;
inline constexpr auto call = call_impl<OP_CALL>;
inline constexpr auto delegatecall = call_impl<OP_DELEGATECALL>;
inline constexpr auto staticcall = call_impl<OP_STATICCALL>;

template <Opcode Op>
Result create_impl(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept;
inline constexpr auto create = create_impl<OP_CREATE>;
inline constexpr auto create2 = create_impl<OP_CREATE2>;

template <qrvmc_status_code StatusCode>
inline TermResult return_impl(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    const auto& offset = stack[0];
    const auto& size = stack[1];

    if (!check_memory(gas_left, state.memory, offset, size))
        return {QRVMC_OUT_OF_GAS, gas_left};

    state.output_size = static_cast<size_t>(size);
    if (state.output_size != 0)
        state.output_offset = static_cast<size_t>(offset);
    return {StatusCode, gas_left};
}
inline constexpr auto return_ = return_impl<QRVMC_SUCCESS>;
inline constexpr auto revert = return_impl<QRVMC_REVERT>;

/// Maps an opcode to the instruction implementation.
///
/// The set of template specializations which map opcodes `Op` to the function
/// implementing the instruction identified by the opcode.
///     instr::impl<OP_DUP1>(/*...*/);
/// The unspecialized template is invalid and should never to used.
template <Opcode Op>
inline constexpr auto impl = nullptr;

#undef ON_OPCODE_IDENTIFIER
#define ON_OPCODE_IDENTIFIER(OPCODE, IDENTIFIER) \
    template <>                                  \
    inline constexpr auto impl<OPCODE> = IDENTIFIER;  // opcode -> implementation
MAP_OPCODES
#undef ON_OPCODE_IDENTIFIER
#define ON_OPCODE_IDENTIFIER ON_OPCODE_IDENTIFIER_DEFAULT
}  // namespace instr::core
}  // namespace qrvmone
