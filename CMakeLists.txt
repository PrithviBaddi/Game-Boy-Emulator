cmake_minimum_required(VERSION 3.20)
project(pocketglass VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(gbcore STATIC core/src/gb_bits.c core/src/gb_cartridge.c core/src/gb_memory.c core/src/gb_cpu.c)
target_include_directories(gbcore PUBLIC core/include)
target_compile_options(gbcore PRIVATE -Wall -Wextra -Wpedantic)

find_package(SDL3 CONFIG REQUIRED)
add_executable(pocketglass app/main.cpp)
target_link_libraries(pocketglass PRIVATE gbcore SDL3::SDL3)

enable_testing()

function(add_gb_test target source test_name)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE gbcore)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_gb_test(gbcore_tests tests/test_bits.c gbcore_bits)
add_gb_test(cartridge_tests tests/test_cartridge.c cartridge_header)
add_gb_test(memory_tests tests/test_memory.c memory_map)
add_gb_test(cpu_tests tests/test_cpu.c cpu_first_instructions)
add_executable(cpu_harness_tests tests/test_cpu_harness.c tests/gb_test_util.c)
target_link_libraries(cpu_harness_tests PRIVATE gbcore)
target_include_directories(cpu_harness_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_options(cpu_harness_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME cpu_harness COMMAND cpu_harness_tests)

add_executable(cpu_demo examples/cpu_demo.c)
target_link_libraries(cpu_demo PRIVATE gbcore)
