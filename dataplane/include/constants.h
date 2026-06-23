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
#define APP_MAX_BRIDGE_DOMAINS    APP_MAX_SEGMENTS

#define DPDK_RX_DESC              1024
#define DPDK_TX_DESC              1024
#define DPDK_MBUF_COUNT           8192
#define DPDK_MBUF_CACHE           256
#define DPDK_BURST                64

#define NEIGH_SIZE 1024
#define FDB_SIZE                  8192
#define VNI_TABLE_SIZE            128
#define GRAPH_FRAME_SIZE          DPDK_BURST
#define GRAPH_FRAME_POOL_SIZE     (APP_MAX_ACCESS_PORTS + APP_MAX_PEERS + 64)
#define ARP_RETRY_SECONDS         1

#define VXLAN_UDP_PORT 4789

#endif
