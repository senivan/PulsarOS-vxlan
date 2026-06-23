#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include "internal.h"

struct vxlan_wire {
    uint8_t flags;
    uint8_t rsvd1[3];
    uint8_t vni[3];
    uint8_t rsvd2;
} __attribute__((packed));

static void pass_all(const struct node_frame *in, struct node_output *out,
                     enum node_id next)
{
    for (uint16_t i = 0; i < in->count; i++)
        node_enqueue(out, next, in->pkts[i], &in->ctxs[i]);
}

void access_rx_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    (void)rt;
    pass_all(in, out, NODE_ACCESS_BIND);
}

void underlay_rx_node_run(struct app_runtime *rt, const struct node_frame *in,
                          struct node_output *out)
{
    (void)rt;
    pass_all(in, out, NODE_ETH_INPUT);
}

void eth_input_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx ctx = in->ctxs[i];
        if (rte_pktmbuf_data_len(m) < sizeof(struct rte_ether_hdr)) {
            node_drop(rt, NODE_ETH_INPUT, m, &ctx, DROP_TRUNCATED_ETH, 1);
            continue;
        }
        const struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, const struct rte_ether_hdr *);
        struct port_state *port = runtime_port(rt, ctx.ingress_port_id);
        if (!port || port->role != PORT_UNDERLAY) {
            node_drop(rt, NODE_ETH_INPUT, m, &ctx, DROP_PORT_BINDING, 1);
            continue;
        }
        ctx.ingress_type = INGRESS_UNDERLAY;
        ctx.l3_offset = sizeof(*eth);
        if (eth->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) &&
            !rte_is_same_ether_addr(&eth->dst_addr, &port->mac)) {
            node_drop(rt, NODE_ETH_INPUT, m, &ctx, DROP_NOT_LOCAL, 0);
            continue;
        }
        if (eth->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP))
            node_enqueue(out, NODE_ARP_INPUT, m, &ctx);
        else if (eth->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
            node_enqueue(out, NODE_IPV4_INPUT, m, &ctx);
        else
            node_drop(rt, NODE_ETH_INPUT, m, &ctx, DROP_UNSUPPORTED_ETHERTYPE, 0);
    }
}

static int valid_sender_ip(const struct port_state *port, uint32_t ip_be)
{
    if (!ip_be) return 0;
    uint32_t ip = rte_be_to_cpu_32(ip_be);
    uint32_t local = rte_be_to_cpu_32(port->ip_be);
    uint32_t mask = port->prefix_len == 32 ? UINT32_MAX : UINT32_MAX << (32 - port->prefix_len);
    return ip != local && (ip & mask) == (local & mask);
}

void arp_input_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx ctx = in->ctxs[i];
        if (rte_pktmbuf_data_len(m) < sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr)) {
            node_drop(rt, NODE_ARP_INPUT, m, &ctx, DROP_INVALID_ARP, 1);
            continue;
        }
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
        struct rte_arp_hdr *arp = (struct rte_arp_hdr *)(eth + 1);
        struct port_state *port = runtime_port(rt, ctx.ingress_port_id);
        if (!port || arp->arp_hardware != rte_cpu_to_be_16(RTE_ARP_HRD_ETHER) ||
            arp->arp_protocol != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) ||
            arp->arp_hlen != RTE_ETHER_ADDR_LEN || arp->arp_plen != 4 ||
            rte_is_zero_ether_addr(&arp->arp_data.arp_sha) ||
            rte_is_multicast_ether_addr(&arp->arp_data.arp_sha) ||
            !rte_is_same_ether_addr(&eth->src_addr, &arp->arp_data.arp_sha)) {
            node_drop(rt, NODE_ARP_INPUT, m, &ctx, DROP_INVALID_ARP, 1);
            continue;
        }
        if (valid_sender_ip(port, arp->arp_data.arp_sip))
            neigh_learn(&rt->neigh, port->port_id, arp->arp_data.arp_sip,
                        &arp->arp_data.arp_sha, rte_get_tsc_cycles());
        if (arp->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY) &&
            arp->arp_data.arp_tip == port->ip_be) {
            rte_pktmbuf_free(m);
            continue;
        }
        if (arp->arp_opcode != rte_cpu_to_be_16(RTE_ARP_OP_REQUEST) ||
            arp->arp_data.arp_tip != port->ip_be) {
            node_drop(rt, NODE_ARP_INPUT, m, &ctx, DROP_INVALID_ARP, 0);
            continue;
        }
        struct rte_ether_addr requester = arp->arp_data.arp_sha;
        uint32_t requester_ip = arp->arp_data.arp_sip;
        eth->dst_addr = requester;
        eth->src_addr = port->mac;
        arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
        arp->arp_data.arp_tha = requester;
        arp->arp_data.arp_tip = requester_ip;
        arp->arp_data.arp_sha = port->mac;
        arp->arp_data.arp_sip = port->ip_be;
        ctx.egress_port_id = port->port_id;
        node_enqueue(out, NODE_UNDERLAY_TX, m, &ctx);
    }
}

void ipv4_input_node_run(struct app_runtime *rt, const struct node_frame *in,
                         struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx ctx = in->ctxs[i];
        if (rte_pktmbuf_data_len(m) < ctx.l3_offset + sizeof(struct rte_ipv4_hdr)) {
            node_drop(rt, NODE_IPV4_INPUT, m, &ctx, DROP_INVALID_IPV4, 1);
            continue;
        }
        const struct rte_ipv4_hdr *ip =
            rte_pktmbuf_mtod_offset(m, const struct rte_ipv4_hdr *, ctx.l3_offset);
        uint16_t ihl = (uint16_t)((ip->version_ihl & 0x0f) * 4);
        uint16_t total = rte_be_to_cpu_16(ip->total_length);
        struct port_state *port = runtime_port(rt, ctx.ingress_port_id);
        if ((ip->version_ihl >> 4) != 4 || ihl < sizeof(*ip) ||
            rte_pktmbuf_data_len(m) < ctx.l3_offset + ihl || total < ihl ||
            rte_pktmbuf_pkt_len(m) < ctx.l3_offset + total || !port ||
            ip->dst_addr != port->ip_be || rte_ipv4_cksum(ip) != 0) {
            enum drop_reason reason = ip->dst_addr != (port ? port->ip_be : 0)
                                      ? DROP_NOT_LOCAL : DROP_INVALID_IPV4;
            node_drop(rt, NODE_IPV4_INPUT, m, &ctx, reason, 1);
            continue;
        }
        if (rte_be_to_cpu_16(ip->fragment_offset) &
            (RTE_IPV4_HDR_MF_FLAG | RTE_IPV4_HDR_OFFSET_MASK)) {
            node_drop(rt, NODE_IPV4_INPUT, m, &ctx, DROP_IPV4_FRAGMENT, 0);
            continue;
        }
        if (ip->next_proto_id != IPPROTO_UDP) {
            node_drop(rt, NODE_IPV4_INPUT, m, &ctx, DROP_UNSUPPORTED_ETHERTYPE, 0);
            continue;
        }
        ctx.outer_src_ip = ip->src_addr;
        ctx.outer_dst_ip = ip->dst_addr;
        ctx.l4_offset = (uint16_t)(ctx.l3_offset + ihl);
        uint32_t excess = rte_pktmbuf_pkt_len(m) - (ctx.l3_offset + total);
        if (excess > UINT16_MAX ||
            (excess && rte_pktmbuf_trim(m, (uint16_t)excess) < 0)) {
            node_drop(rt, NODE_IPV4_INPUT, m, &ctx, DROP_INVALID_IPV4, 1);
            continue;
        }
        node_enqueue(out, NODE_UDP_INPUT, m, &ctx);
    }
}

void udp_input_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx ctx = in->ctxs[i];
        if (rte_pktmbuf_data_len(m) < ctx.l4_offset + sizeof(struct rte_udp_hdr)) {
            node_drop(rt, NODE_UDP_INPUT, m, &ctx, DROP_INVALID_UDP, 1);
            continue;
        }
        const struct rte_udp_hdr *udp =
            rte_pktmbuf_mtod_offset(m, const struct rte_udp_hdr *, ctx.l4_offset);
        const struct rte_ipv4_hdr *ip =
            rte_pktmbuf_mtod_offset(m, const struct rte_ipv4_hdr *, ctx.l3_offset);
        uint16_t len = rte_be_to_cpu_16(udp->dgram_len);
        if (udp->dst_port != rte_cpu_to_be_16(rt->vxlan_udp_port) ||
            len < sizeof(*udp) + sizeof(struct vxlan_wire) ||
            rte_pktmbuf_pkt_len(m) < ctx.l4_offset + len ||
            (udp->dgram_cksum && rte_ipv4_udptcp_cksum_verify(ip, udp) < 0)) {
            node_drop(rt, NODE_UDP_INPUT, m, &ctx, DROP_INVALID_UDP, 1);
            continue;
        }
        ctx.inner_offset = (uint16_t)(ctx.l4_offset + sizeof(*udp) + sizeof(struct vxlan_wire));
        node_enqueue(out, NODE_VXLAN_VALIDATE, m, &ctx);
    }
}

void vxlan_validate_node_run(struct app_runtime *rt, const struct node_frame *in,
                             struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx ctx = in->ctxs[i];
        uint16_t off = (uint16_t)(ctx.inner_offset - sizeof(struct vxlan_wire));
        if (rte_pktmbuf_data_len(m) < ctx.inner_offset + sizeof(struct rte_ether_hdr)) {
            node_drop(rt, NODE_VXLAN_VALIDATE, m, &ctx, DROP_INVALID_VXLAN, 1);
            continue;
        }
        const struct vxlan_wire *vx =
            rte_pktmbuf_mtod_offset(m, const struct vxlan_wire *, off);
        if (vx->flags != 0x08 || vx->rsvd1[0] || vx->rsvd1[1] ||
            vx->rsvd1[2] || vx->rsvd2) {
            node_drop(rt, NODE_VXLAN_VALIDATE, m, &ctx, DROP_INVALID_VXLAN, 1);
            continue;
        }
        ctx.vni = ((uint32_t)vx->vni[0] << 16) |
                  ((uint32_t)vx->vni[1] << 8) | vx->vni[2];
        node_enqueue(out, NODE_VXLAN_DECAP, m, &ctx);
    }
}

void vxlan_decap_node_run(struct app_runtime *rt, const struct node_frame *in,
                          struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        if (!rte_pktmbuf_adj(in->pkts[i], ctx.inner_offset)) {
            node_drop(rt, NODE_VXLAN_DECAP, in->pkts[i], &ctx, DROP_INVALID_VXLAN, 1);
            continue;
        }
        in->pkts[i]->ol_flags = 0;
        in->pkts[i]->l2_len = 0;
        in->pkts[i]->l3_len = 0;
        in->pkts[i]->l4_len = 0;
        ctx.ingress_type = INGRESS_VXLAN;
        node_enqueue(out, NODE_VNI_LOOKUP, in->pkts[i], &ctx);
    }
}

void vni_lookup_node_run(struct app_runtime *rt, const struct node_frame *in,
                         struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        const struct bridge_domain *bd = runtime_vni_lookup(rt, ctx.vni);
        if (!bd) {
            node_drop(rt, NODE_VNI_LOOKUP, in->pkts[i], &ctx, DROP_UNKNOWN_VNI, 0);
            continue;
        }
        if (bd->underlay_port_id != ctx.ingress_port_id ||
            !runtime_peer_allowed(bd, ctx.outer_src_ip)) {
            node_drop(rt, NODE_VNI_LOOKUP, in->pkts[i], &ctx, DROP_UNKNOWN_VTEP, 0);
            continue;
        }
        ctx.bd_id = bd->bd_id;
        ctx.remote_vtep_ip = ctx.outer_src_ip;
        node_enqueue(out, NODE_BRIDGE_DOMAIN_INPUT, in->pkts[i], &ctx);
    }
}
