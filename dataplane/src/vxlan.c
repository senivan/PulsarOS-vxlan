#include <string.h>
#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include "runtime.h"
#include "vxlan.h"

struct vxlan_hdr { uint8_t flags, rsvd1[3], vni[3], rsvd2; } __attribute__((packed));

int vxlan_encapsulate(struct app_runtime *rt, struct rte_mbuf *m, struct packet_ctx *ctx)
{
    struct port_state *port = runtime_port(rt, ctx->egress_port_id);
    const uint16_t outer_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) +
                               sizeof(struct rte_udp_hdr) + sizeof(struct vxlan_hdr);
    uint32_t inner_len = rte_pktmbuf_pkt_len(m);
    uint32_t ip_len = inner_len + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(struct vxlan_hdr);
    if (!port || ip_len > port->mtu || ip_len > UINT16_MAX) return -2;
    void *head = rte_pktmbuf_prepend(m, outer_len);
    if (!head) return -1;
    m->ol_flags = 0;
    struct rte_ether_hdr *eth = head;
    memset(eth, 0, sizeof(*eth));
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->type_of_service = 0;
    ip->total_length = rte_cpu_to_be_16((uint16_t)ip_len);
    ip->fragment_offset = rte_cpu_to_be_16(RTE_IPV4_HDR_DF_FLAG);
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = port->ip_be;
    ip->dst_addr = ctx->remote_vtep_ip;
    ip->hdr_checksum = rte_ipv4_cksum(ip);
    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < RTE_ETHER_ADDR_LEN; i++) {
        h = (h ^ ctx->inner_src_mac.addr_bytes[i]) * 16777619u;
        h = (h ^ ctx->inner_dst_mac.addr_bytes[i]) * 16777619u;
    }
    udp->src_port = rte_cpu_to_be_16((uint16_t)(49152u + (h & 0x3fffu)));
    udp->dst_port = rte_cpu_to_be_16(rt->vxlan_udp_port);
    udp->dgram_len = rte_cpu_to_be_16((uint16_t)(ip_len - sizeof(*ip)));
    udp->dgram_cksum = 0;
    struct vxlan_hdr *vx = (struct vxlan_hdr *)(udp + 1);
    memset(vx, 0, sizeof(*vx));
    vx->flags = 0x08;
    vx->vni[0] = (uint8_t)(ctx->vni >> 16);
    vx->vni[1] = (uint8_t)(ctx->vni >> 8);
    vx->vni[2] = (uint8_t)ctx->vni;
    m->l2_len = sizeof(*eth);
    m->l3_len = sizeof(*ip);
    return 0;
}

struct rte_mbuf *arp_build_request(struct app_runtime *rt, uint16_t port_id,
                                   uint32_t target_ip_be)
{
    struct port_state *port = runtime_port(rt, port_id);
    if (!port) return NULL;
    struct rte_mbuf *m = rte_pktmbuf_alloc(rt->mbuf_pool);
    uint16_t len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
    if (!m || !rte_pktmbuf_append(m, len)) { if (m) rte_pktmbuf_free(m); return NULL; }
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    struct rte_arp_hdr *arp = (struct rte_arp_hdr *)(eth + 1);
    memset(eth->dst_addr.addr_bytes, 0xff, RTE_ETHER_ADDR_LEN);
    eth->src_addr = port->mac;
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);
    arp->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp->arp_plen = 4;
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    arp->arp_data.arp_sha = port->mac;
    arp->arp_data.arp_sip = port->ip_be;
    memset(&arp->arp_data.arp_tha, 0, sizeof(arp->arp_data.arp_tha));
    arp->arp_data.arp_tip = target_ip_be;
    return m;
}
