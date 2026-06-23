#pragma once
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <rte_ether.h>

enum ingress_type { INGRESS_ACCESS, INGRESS_UNDERLAY, INGRESS_VXLAN };
enum l2_result { L2_LOCAL_ACCESS, L2_REMOTE_VTEP, L2_BUM, L2_DROP };

enum drop_reason {
    DROP_NONE, DROP_PORT_BINDING, DROP_TRUNCATED_ETH, DROP_UNSUPPORTED_ETHERTYPE,
    DROP_INVALID_ARP, DROP_INVALID_IPV4, DROP_IPV4_FRAGMENT, DROP_NOT_LOCAL,
    DROP_INVALID_UDP, DROP_INVALID_VXLAN, DROP_UNKNOWN_VNI, DROP_UNKNOWN_VTEP,
    DROP_INVALID_INNER_ETH, DROP_SAME_PORT, DROP_SPLIT_HORIZON, DROP_FLOOD_DISABLED,
    DROP_NO_FLOOD_TARGET, DROP_NEIGH_MISS, DROP_NO_HEADROOM, DROP_MTU,
    DROP_REPLICA_ALLOC, DROP_GRAPH_FULL, DROP_TX_FAILED, DROP_REASON_MAX
};

enum packet_flags { PKT_F_BUM = 1u << 0, PKT_F_ARP_REPLY = 1u << 1 };

struct packet_ctx {
    uint16_t ingress_port_id, egress_port_id, bd_id, flags;
    uint16_t l3_offset, l4_offset, inner_offset, inner_ethertype;
    uint32_t vni, outer_src_ip, outer_dst_ip, remote_vtep_ip, next_hop_ip;
    struct rte_ether_addr inner_src_mac, inner_dst_mac, next_hop_mac;
    uint8_t ingress_type, l2_result, drop_reason;
};

#endif
