# qrvmone: Fast Quantum Resistant Virtual Machine implementation
# Copyright 2018 The evmone Authors.
# SPDX-License-Identifier: Apache-2.0

include(hunter_cmake_args)

hunter_cmake_args(
    ethash
    CMAKE_ARGS -DETHASH_BUILD_ETHASH=OFF
)

hunter_config(
    intx
    VERSION 0.12.0
    URL https://github.com/chfast/intx/archive/v0.12.0.tar.gz
    SHA1 18a64e8e88c50d53325d906c9211daef905b97f4
)

# CMake 4 removed compatibility with cmake_minimum_required(VERSION) below 3.5, which these Hunter
# 0.25.3 packages still declare (CLI11 comes from the qrvmc tools in test builds). Configure them with
# the 3.5 policy defaults; older CMake versions ignore the setting. hunter_config, unlike
# hunter_cmake_args, is not overridden by a package recipe's own defaults.
foreach(package CLI11 nlohmann_json)
    hunter_config(
        ${package}
        VERSION ${HUNTER_${package}_VERSION}
        CMAKE_ARGS CMAKE_POLICY_VERSION_MINIMUM=3.5
    )
endforeach()
