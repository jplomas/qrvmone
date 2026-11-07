// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// SPDX-License-Identifier: Apache-2.0

#include "statetest.hpp"

namespace qrvmone::test
{
/// See statetest.hpp: the JSON allocation form that from_json<state::State>() reads.
json::json export_state(const state::State& state)
{
    auto j = json::json::object();
    for (const auto& [addr, acc] : state.get_accounts())
    {
        auto& j_acc = j[hexq(addr)];
        j_acc["nonce"] = hex0x(acc.nonce);
        j_acc["balance"] = hex0x(acc.balance);
        j_acc["code"] = hex0x(bytes_view{acc.code.data(), acc.code.size()});
        for (const auto& [key, value] : acc.storage)
        {
            // Storage keys are 32 bytes held in the low half of the 64-byte word; write them
            // as go-qrl's storageJSON does, so go-qrl reads this allocation too.
            if (!is_zero(value.current))
                j_acc["storage"][hex0x(bytes_view{key.bytes + 32, 32})] = hex0x(value.current);
        }
    }
    return j;
}
}  // namespace qrvmone::test
