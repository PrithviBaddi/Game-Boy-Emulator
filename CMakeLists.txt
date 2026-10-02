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
add_executable(gbcore_tests tests/test_bits.c)
target_link_libraries(gbcore_tests PRIVATE gbcore)
add_test(NAME gbcore_bits COMMAND gbcore_tests)

add_executable(cartridge_tests tests/test_cartridge.c)
target_link_libraries(cartridge_tests PRIVATE gbcore)
add_test(NAME cartridge_header COMMAND cartridge_tests)

add_executable(memory_tests tests/test_memory.c)
target_link_libraries(memory_tests PRIVATE gbcore)
add_test(NAME memory_map COMMAND memory_tests)

add_executable(cpu_tests tests/test_cpu.c)
target_link_libraries(cpu_tests PRIVATE gbcore)
add_test(NAME cpu_first_instructions COMMAND cpu_tests)

add_executable(cpu_demo examples/cpu_demo.c)
target_link_libraries(cpu_demo PRIVATE gbcore)
