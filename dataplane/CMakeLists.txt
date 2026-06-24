cmake_minimum_required(VERSION 3.16)
project(pulsaros-vxlan-dataplane C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

add_library(vxlan-dpdk STATIC
  src/config.c
  src/dpdk_port.c
  src/fdb.c
  src/graph.c
  src/nodes/drop.c
  src/nodes/encap.c
  src/nodes/ingress.c
  src/nodes/l2.c
  src/nodes/register.c
  src/nodes/tx.c
  src/runtime.c
  src/runtime_tables.c
  src/vxlan.c
)

target_include_directories(vxlan-dpdk PUBLIC
  include
  third_party/jsmn
  ${DPDK_INCLUDE_DIRS}
)

target_link_libraries(vxlan-dpdk PUBLIC
  ${DPDK_LIBRARIES}
)

target_compile_options(vxlan-dpdk PUBLIC
  ${DPDK_CFLAGS_OTHER}
  -Wall -Wextra -Wpedantic
)

add_executable(vxlan-dp
  src/main.c
)

target_link_libraries(vxlan-dp PRIVATE
  vxlan-dpdk
)
