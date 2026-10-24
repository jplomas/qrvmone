// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "advanced_execution.hpp"
#include "advanced_analysis.hpp"
#include <memory>

namespace qrvmone::advanced
{
qrvmc_result execute(AdvancedExecutionState& state, const AdvancedCodeAnalysis& analysis) noexcept
{
    state.analysis.advanced = &analysis;  // Allow accessing the analysis by instructions.

    const auto* instr = state.analysis.advanced->instrs.data();  // Get the first instruction.
    while (instr != nullptr)
        instr = instr->fn(instr, state);

    const auto gas_left =
        (state.status == QRVMC_SUCCESS || state.status == QRVMC_REVERT) ? state.gas_left : 0;
    const auto gas_refund = (state.status == QRVMC_SUCCESS) ? state.gas_refund : 0;

    assert(state.output_size != 0 || state.output_offset == 0);
    return qrvmc::make_result(state.status, gas_left, gas_refund,
        state.memory.data() + state.output_offset, state.output_size);
}

qrvmc_result execute(qrvmc_vm* /*unused*/, const qrvmc_host_interface* host, qrvmc_host_context* ctx,
    qrvmc_revision rev, const qrvmc_message* msg, const uint8_t* code, size_t code_size) noexcept
{
    // Instruction and gas tables are indexed by revision. Reject one this VM was not built for,
    // for example from a host built against other QRVMC headers, instead of reading past them.
    if (static_cast<unsigned>(rev) > QRVMC_MAX_REVISION) [[unlikely]]
        return qrvmc_make_result(QRVMC_REJECTED, 0, 0, nullptr, 0);

    AdvancedCodeAnalysis analysis;
    const bytes_view container = {code, code_size};
    analysis = analyze(rev, container);
    auto state = std::make_unique<AdvancedExecutionState>(*msg, rev, *host, ctx, container);
    return execute(*state, analysis);
}
}  // namespace qrvmone::advanced
