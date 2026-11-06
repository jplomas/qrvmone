# qrvmone: Fast Quantum Resistant Virtual Machine implementation
# Copyright 2022 The evmone Authors.
# SPDX-License-Identifier: Apache-2.0

hunter_add_package(nlohmann_json)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(qrvmone-statetestutils STATIC)
add_library(qrvmone::statetestutils ALIAS qrvmone-statetestutils)
target_compile_features(qrvmone-statetestutils PUBLIC cxx_std_20)
target_link_libraries(qrvmone-statetestutils PRIVATE qrvmone::state nlohmann_json::nlohmann_json)
target_include_directories(qrvmone-statetestutils PRIVATE ${qrvmone_private_include_dir})
target_sources(
    qrvmone-statetestutils PRIVATE
    statetest.hpp
    statetest_export.cpp
    statetest_loader.cpp
    statetest_logs_hash.cpp
)

add_executable(qrvmone-statetest)
target_link_libraries(qrvmone-statetest PRIVATE qrvmone::statetestutils qrvmone GTest::gtest)
target_include_directories(qrvmone-statetest PRIVATE ${qrvmone_private_include_dir})
target_sources(
    qrvmone-statetest PRIVATE
    statetest.cpp
    statetest_runner.cpp
)
