# qrvmone: Fast Quantum Resistant Virtual Machine implementation
# Copyright 2019-2020 The evmone Authors.
# SPDX-License-Identifier: Apache-2.0

include(GoogleTest)
include(${PROJECT_SOURCE_DIR}/qrvmc/cmake/QRVMC.cmake)

set(qrvmone_private_include_dir ${PROJECT_SOURCE_DIR}/lib)

hunter_add_package(GTest)
find_package(GTest CONFIG REQUIRED)

hunter_add_package(benchmark)
find_package(benchmark CONFIG REQUIRED)

add_subdirectory(utils)
add_subdirectory(bench)
add_subdirectory(conformance)
add_subdirectory(integration)
add_subdirectory(internal_benchmarks)
add_subdirectory(state)
add_subdirectory(statetest)
add_subdirectory(t8n)
add_subdirectory(unittests)

set(targets qrvmone-bench qrvmone-bench-internal qrvmone-conformance qrvmone-state qrvmone-statetest qrvmone-t8n qrvmone-unittests)

if(QRVMONE_FUZZING)
    add_subdirectory(fuzzer)
    list(APPEND targets qrvmone-fuzzer)
endif()

set_target_properties(
    ${targets} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
)

get_target_property(type qrvmone TYPE)
if(type STREQUAL SHARED_LIBRARY)
    qrvmc_add_vm_test(NAME ${PROJECT_NAME}/qrvmc-vmtester TARGET qrvmone)

    if(TARGET qrvm-test)
        add_test(NAME ${PROJECT_NAME}/qrvm-test COMMAND qrvm-test $<TARGET_FILE:qrvmone>)
    endif()
endif()
