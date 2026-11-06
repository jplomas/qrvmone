# qrvmone: Fast Quantum Resistant Virtual Machine implementation
# SPDX-License-Identifier: Apache-2.0

add_executable(qrvmone-conformance conformance.cpp)
target_link_libraries(qrvmone-conformance PRIVATE qrvmone qrvmc::mocked_host)
