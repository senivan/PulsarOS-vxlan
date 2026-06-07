#pragma once
#ifndef APP_H
#define APP_H
#include <stdint.h>
#include "constants.h"

enum app_pmd {
    PMD_TAP,
    PMD_AFPKT,
    PMD_PHYS
};

enum app_port_role {
    PORT_UNDERLAY,
    PORT_ACCESS
};

enum app_controlplane_mode {
    CONTROLPLANE_STATIC,
    CONTROLPLANE_RUNTIME,
    CONTROLPLANE_STATIC_AND_RUNTIME
};

struct interface {
    char name[IF_NAME_MAX_LEN];
    char pcie_addr[APP_PCI_ADDR_MAX_LEN];
    uint32_t ip_addr;
};

struct app_port_config {
    char name[APP_NAME_MAX_LEN];
    enum app_port_role role;
    enum app_pmd pmd;
    char iface[IF_NAME_MAX_LEN];
    char pci_addr[APP_PCI_ADDR_MAX_LEN];
    uint32_t ip_be;
    uint8_t prefix_len;
    uint8_t has_ip;
};

struct app_vxlan_peer {
    uint32_t ip_be;
};

struct app_vxlan_segment {
    char name[APP_NAME_MAX_LEN];
    uint32_t vni;
    uint16_t underlay_port;
    uint16_t access_ports[APP_MAX_ACCESS_PORTS];
    uint16_t access_port_count;
    struct app_vxlan_peer peers[APP_MAX_PEERS];
    uint16_t peer_count;
};

struct app_config {
    enum app_pmd pmd;
    struct interface underlay;
    uint64_t lcore_mask;
    char lcores[APP_LCORES_MAX_LEN];
    int socket_id;
    uint32_t mbufs;
    uint32_t mbuf_cache;
    uint16_t rx_desc;
    uint16_t tx_desc;
    uint16_t port_count;
    struct app_port_config ports[APP_MAX_PORTS];
    uint16_t vxlan_udp_port;
    enum app_controlplane_mode controlplane;
    uint16_t segment_count;
    struct app_vxlan_segment segments[APP_MAX_SEGMENTS];
};

#endif
