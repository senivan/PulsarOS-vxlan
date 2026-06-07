#pragma once
#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <stdint.h>

#define IF_NAME_MAX_LEN           64
#define APP_NAME_MAX_LEN          64
#define APP_LCORES_MAX_LEN        64
#define APP_PCI_ADDR_MAX_LEN      32

#define APP_MAX_PORTS             16
#define APP_MAX_SEGMENTS          64
#define APP_MAX_ACCESS_PORTS      16
#define APP_MAX_PEERS             64

#define DPDK_RX_DESC              1024
#define DPDK_TX_DESC              1024
#define DPDK_MBUF_COUNT           8192
#define DPDK_MBUF_CACHE           256
#define DPDK_BURST                64

#define NEIGH_SIZE 1024

#define VXLAN_UDP_PORT 4789

#endif
