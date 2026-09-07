cmake_minimum_required(VERSION 3.20)
project(phalanx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -march=native -pipe)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(LIBBPF REQUIRED libbpf)
find_package(fmt REQUIRED)

include_directories(include ${LIBURING_INCLUDE_DIRS} ${LIBBPF_INCLUDE_DIRS})

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/xdp_filter.o
    COMMAND clang -O2 -target bpf -g -c ${CMAKE_SOURCE_DIR}/ebpf/xdp_filter.bpf.c -o ${CMAKE_BINARY_DIR}/xdp_filter.o
    DEPENDS ${CMAKE_SOURCE_DIR}/ebpf/xdp_filter.bpf.c
    COMMENT "[eBPF] Компиляция ядра xdp_filter.bpf.c в байткод BPF"
)
add_custom_target(ebpf_target ALL DEPENDS ${CMAKE_BINARY_DIR}/xdp_filter.o)

add_library(phalanx_ebpf STATIC src/ebpf/xdp_manager.cpp)
target_link_libraries(phalanx_ebpf PUBLIC ${LIBBPF_LIBRARIES} fmt::fmt)

add_library(phalanx_proxy STATIC src/proxy/upstream.cpp)
target_link_libraries(phalanx_proxy PUBLIC fmt::fmt)

add_library(phalanx_core STATIC src/core/worker.cpp src/core/engine.cpp)
target_link_libraries(phalanx_core PUBLIC phalanx_proxy Threads::Threads ${LIBURING_LIBRARIES} fmt::fmt)

add_executable(phalanx src/main.cpp)
add_dependencies(phalanx ebpf_target)
target_link_libraries(phalanx PRIVATE phalanx_core phalanx_ebpf ${LIBBPF_LIBRARIES})
