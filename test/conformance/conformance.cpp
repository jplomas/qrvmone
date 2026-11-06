// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// SPDX-License-Identifier: Apache-2.0

/// Cross-implementation conformance runner.
///
/// Executes the go-qrl VM conformance corpus (go-qrl core/vm/conformance) on
/// qrvmone so the two runtimes can be compared vector by vector. The go-qrl
/// side drives this binary: see core/vm/conformance/cross_test.go there.
///
/// Input on stdin, one vector per line, tab separated:
///     name <TAB> bytecode-hex <TAB> input-hex <TAB> gas-limit
/// Output on stdout, one result per input line, tab separated:
///     name <TAB> return-hex <TAB> error-class <TAB> gas-used
///
/// The execution environment mirrors the go-qrl runner: a single CALL to a
/// contract holding the bytecode, from a fixed origin, on an empty state, with
/// the sender and recipient already warm. Error classes use the portable names
/// of go-qrl's conformance.ErrorClass.

#include <qrvmc/hex.hpp>
#include <qrvmc/mocked_host.hpp>
#include <qrvmone/qrvmone.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
/// Returns the 64-byte address with the given bytes right-aligned, like
/// go-qrl common.BytesToAddress.
qrvmc::address address_from_bytes(std::string_view s) noexcept
{
    qrvmc::address addr{};
    std::memcpy(&addr.bytes[sizeof(addr.bytes) - s.size()], s.data(), s.size());
    return addr;
}

const char* error_class(qrvmc_status_code status) noexcept
{
    switch (status)
    {
    case QRVMC_SUCCESS:
        return "";
    case QRVMC_REVERT:
        return "reverted";
    case QRVMC_OUT_OF_GAS:
        return "out-of-gas";
    case QRVMC_STACK_UNDERFLOW:
        return "stack-underflow";
    case QRVMC_STACK_OVERFLOW:
        return "stack-overflow";
    case QRVMC_BAD_JUMP_DESTINATION:
        return "invalid-jump";
    case QRVMC_INVALID_INSTRUCTION:
    case QRVMC_UNDEFINED_INSTRUCTION:
        return "invalid-opcode";
    case QRVMC_STATIC_MODE_VIOLATION:
        return "write-protection";
    default:
        return "other";
    }
}

bool decode_hex(const std::string& hex, qrvmc::bytes& out)
{
    out.clear();
    return qrvmc::from_hex(hex.begin(), hex.end(), std::back_inserter(out));
}

std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in{line};
    while (std::getline(in, field, '\t'))
        fields.push_back(field);
    if (!line.empty() && line.back() == '\t')
        fields.emplace_back();
    return fields;
}
}  // namespace

int main()
{
    qrvmc::VM vm{qrvmc_create_qrvmone()};
    const auto contract = address_from_bytes("conformance");
    const auto origin = address_from_bytes("origin");

    std::string line;
    int line_no = 0;
    while (std::getline(std::cin, line))
    {
        ++line_no;
        if (line.empty())
            continue;
        const auto fields = split_tabs(line);
        qrvmc::bytes code;
        qrvmc::bytes input;
        if (fields.size() != 4 || !decode_hex(fields[1], code) || !decode_hex(fields[2], input))
        {
            std::cerr << "line " << line_no << ": malformed vector\n";
            return 2;
        }
        const auto gas_limit = std::stoll(fields[3]);

        qrvmc::MockedHost host;
        host.tx_context.tx_origin = origin;
        host.accounts[contract].code = code;

        qrvmc_message msg{};
        msg.kind = QRVMC_CALL;
        msg.gas = gas_limit;
        msg.recipient = contract;
        msg.code_address = contract;
        msg.sender = origin;
        msg.input_data = input.data();
        msg.input_size = input.size();
        host.access_account(msg.sender);
        host.access_account(msg.recipient);

        const auto result = vm.execute(host, QRVMC_ZOND, msg, code.data(), code.size());

        // go-qrl returns no data for an exceptional halt and the output for
        // success or revert.
        const bool has_output =
            result.status_code == QRVMC_SUCCESS || result.status_code == QRVMC_REVERT;
        const auto output = has_output ? qrvmc::bytes_view{result.output_data, result.output_size} :
                                         qrvmc::bytes_view{};
        // An exceptional halt consumes all gas.
        const auto gas_left = has_output ? result.gas_left : 0;

        std::cout << fields[0] << '\t' << qrvmc::hex(output) << '\t'
                  << error_class(result.status_code) << '\t' << (gas_limit - gas_left) << '\n';
    }
    return 0;
}
