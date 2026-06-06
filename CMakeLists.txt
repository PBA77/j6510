cmake_minimum_required(VERSION 3.16)

project(j6510 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(J6510_ENABLE_BLOCK_CACHE "Enable cached-block execution support" ON)

add_library(j6510_core
    src/cpu6510_bus.cpp
    src/cpu6510_core.cpp
    src/cpu6510_opcode_table.cpp
)

target_include_directories(j6510_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(j6510_core
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

target_compile_definitions(j6510_core
    PUBLIC
        J6510_ENABLE_BLOCK_CACHE=$<BOOL:${J6510_ENABLE_BLOCK_CACHE}>
)

enable_testing()

add_executable(j6510_tests
    tests/test_main.cpp
)

target_link_libraries(j6510_tests PRIVATE j6510_core)

add_test(NAME j6510_tests COMMAND j6510_tests)

add_executable(j6510_benchmark
    tools/benchmark_main.cpp
)

target_link_libraries(j6510_benchmark PRIVATE j6510_core)

set(KLAUS_FUNCTIONAL_IMAGE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klaus/6502_functional_test.bin)
set(KLAUS_DECIMAL_IMAGE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klaus/6502_decimal_test.bin)
set(KLAUS_INTERRUPT_IMAGE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/klaus/6502_interrupt_test.bin)

add_executable(klaus_functional_test tests/klaus_functional_main.cpp)
target_link_libraries(klaus_functional_test PRIVATE j6510_core)

if(EXISTS ${KLAUS_FUNCTIONAL_IMAGE})
    add_test(NAME klaus_functional_test COMMAND klaus_functional_test ${KLAUS_FUNCTIONAL_IMAGE})
else()
    message(STATUS "Klaus functional test image not found; run scripts/fetch_external_tests.sh to enable it")
endif()

add_executable(klaus_decimal_test tests/klaus_decimal_main.cpp)
target_link_libraries(klaus_decimal_test PRIVATE j6510_core)

if(EXISTS ${KLAUS_DECIMAL_IMAGE})
    add_test(NAME klaus_decimal_test COMMAND klaus_decimal_test ${KLAUS_DECIMAL_IMAGE})
else()
    message(STATUS "Klaus decimal test image not found; run scripts/fetch_external_tests.sh to enable it")
endif()

add_executable(klaus_interrupt_test tests/klaus_interrupt_main.cpp)
target_link_libraries(klaus_interrupt_test PRIVATE j6510_core)

if(EXISTS ${KLAUS_INTERRUPT_IMAGE})
    add_test(NAME klaus_interrupt_test COMMAND klaus_interrupt_test ${KLAUS_INTERRUPT_IMAGE})
else()
    message(STATUS "Klaus interrupt test image not found; run scripts/fetch_external_tests.sh to enable it")
endif()
