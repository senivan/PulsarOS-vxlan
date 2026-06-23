#include <rte_cycles.h>
#include "internal.h"
#include "vxlan.h"

void bum_replication_node_run(struct app_runtime *rt, const struct node_frame *in,
                              struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct rte_mbuf *m = in->pkts[i];
        struct packet_ctx base = in->ctxs[i];
        const struct bridge_domain *bd = runtime_bd(rt, base.bd_id);
        unsigned made = 0;
        unsigned targets = 0;

        for (uint16_t p = 0; p < bd->access_port_count; p++) {
            if (bd->access_ports[p] == base.ingress_port_id) continue;
            targets++;
            struct rte_mbuf *copy = rte_pktmbuf_clone(m, rt->mbuf_pool);
            if (!copy) {
                rt->graph.nodes[NODE_BUM_REPLICATION].stats.errors++;
                rt->graph.nodes[NODE_BUM_REPLICATION].stats.drops++;
                continue;
            }
            struct packet_ctx ctx = base;
            ctx.egress_port_id = bd->access_ports[p];
            node_enqueue(out, NODE_LOCAL_ACCESS_FORWARD, copy, &ctx);
            made++;
        }

        if (base.ingress_type == INGRESS_ACCESS) {
            for (uint16_t p = 0; p < bd->remote_vtep_count; p++) {
                targets++;
                struct rte_mbuf *copy = rte_pktmbuf_copy(m, rt->mbuf_pool, 0, UINT32_MAX);
                if (!copy) {
                    rt->graph.nodes[NODE_BUM_REPLICATION].stats.errors++;
                    rt->graph.nodes[NODE_BUM_REPLICATION].stats.drops++;
                    continue;
                }
                struct packet_ctx ctx = base;
                ctx.remote_vtep_ip = bd->remote_vteps[p];
                ctx.egress_port_id = bd->underlay_port_id;
                node_enqueue(out, NODE_VXLAN_BUM_ENCAP, copy, &ctx);
                made++;
            }
        }

        rte_pktmbuf_free(m);
        if (!made) {
            rt->graph.nodes[NODE_BUM_REPLICATION].stats.drops++;
            rt->graph.drop_reasons[targets ? DROP_REPLICA_ALLOC : DROP_NO_FLOOD_TARGET]++;
        }
    }
}

static void encap_common(struct app_runtime *rt, const struct node_frame *in,
                         struct node_output *out, enum node_id owner)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        int rc = vxlan_encapsulate(rt, in->pkts[i], &ctx);
        if (rc) {
            node_drop(rt, owner, in->pkts[i], &ctx,
                      rc == -2 ? DROP_MTU : DROP_NO_HEADROOM, 1);
            continue;
        }
        node_enqueue(out, NODE_UNDERLAY_NEIGH_LOOKUP, in->pkts[i], &ctx);
    }
}

void vxlan_ucast_encap_node_run(struct app_runtime *rt,
                                const struct node_frame *in,
                                struct node_output *out)
{
    encap_common(rt, in, out, NODE_VXLAN_UCAST_ENCAP);
}

void vxlan_bum_encap_node_run(struct app_runtime *rt,
                              const struct node_frame *in,
                              struct node_output *out)
{
    encap_common(rt, in, out, NODE_VXLAN_BUM_ENCAP);
}

void underlay_neigh_lookup_node_run(struct app_runtime *rt,
                                    const struct node_frame *in,
                                    struct node_output *out)
{
    uint64_t now = rte_get_tsc_cycles();
    uint64_t retry = rte_get_tsc_hz() * ARP_RETRY_SECONDS;
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        ctx.next_hop_ip = ctx.remote_vtep_ip;
        const struct neigh_entry *entry =
            neigh_lookup(&rt->neigh, ctx.egress_port_id, ctx.next_hop_ip);
        if (entry && entry->state == NEIGH_REACHABLE) {
            ctx.next_hop_mac = entry->mac;
            node_enqueue(out, NODE_UNDERLAY_REWRITE, in->pkts[i], &ctx);
            continue;
        }

        int send = neigh_mark_request(&rt->neigh, ctx.egress_port_id,
                                      ctx.next_hop_ip, now, retry);
        if (send > 0) {
            struct rte_mbuf *arp =
                arp_build_request(rt, ctx.egress_port_id, ctx.next_hop_ip);
            if (arp) {
                struct packet_ctx arpctx = { .egress_port_id = ctx.egress_port_id };
                node_enqueue(out, NODE_UNDERLAY_TX, arp, &arpctx);
            } else {
                rt->graph.nodes[NODE_UNDERLAY_NEIGH_LOOKUP].stats.errors++;
            }
        }
        node_drop(rt, NODE_UNDERLAY_NEIGH_LOOKUP, in->pkts[i], &ctx,
                  DROP_NEIGH_MISS, send < 0);
    }
}

void underlay_rewrite_node_run(struct app_runtime *rt,
                               const struct node_frame *in,
                               struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        struct port_state *port = runtime_port(rt, ctx.egress_port_id);
        if (!port || rte_pktmbuf_data_len(in->pkts[i]) < sizeof(struct rte_ether_hdr)) {
            node_drop(rt, NODE_UNDERLAY_REWRITE, in->pkts[i], &ctx,
                      DROP_PORT_BINDING, 1);
            continue;
        }
        struct rte_ether_hdr *eth =
            rte_pktmbuf_mtod(in->pkts[i], struct rte_ether_hdr *);
        eth->src_addr = port->mac;
        eth->dst_addr = ctx.next_hop_mac;
        node_enqueue(out, NODE_UNDERLAY_TX, in->pkts[i], &ctx);
    }
}
