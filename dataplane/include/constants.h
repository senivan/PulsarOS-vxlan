#pragma once
#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <stdint.h>

#define IF_NAME_MAX_LEN           64

#define DPDK_RX_DESC              1024
#define DPDK_TX_DESC              1024
#define DPDK_MBUF_COUNT           8192
#define DPDK_MBUF_CACHE           256
#define DPDK_BURST                64

#define NEIGH_SIZE 1024

#define VXLAN_UDP_PORT 4789

#endif
