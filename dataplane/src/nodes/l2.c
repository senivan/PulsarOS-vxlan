#include <rte_byteorder.h>
#include <rte_cycles.h>
#include "internal.h"

void access_bind_node_run(struct app_runtime *rt, const struct node_frame *in,
                          struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        if (ctx.ingress_port_id >= RTE_MAX_ETHPORTS ||
            !rt->port_bindings[ctx.ingress_port_id].configured ||
            rt->port_bindings[ctx.ingress_port_id].role != PORT_ACCESS ||
            !rt->port_bindings[ctx.ingress_port_id].bd_id) {
            node_drop(rt, NODE_ACCESS_BIND, in->pkts[i], &ctx, DROP_PORT_BINDING, 1);
            continue;
        }
        ctx.ingress_type = INGRESS_ACCESS;
        ctx.bd_id = rt->port_bindings[ctx.ingress_port_id].bd_id;
        const struct bridge_domain *bd = runtime_bd(rt, ctx.bd_id);
        ctx.vni = bd->vni;
        node_enqueue(out, NODE_BRIDGE_DOMAIN_INPUT, in->pkts[i], &ctx);
    }
}

void bridge_domain_input_node_run(struct app_runtime *rt,
                                  const struct node_frame *in,
                                  struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        const struct bridge_domain *bd = runtime_bd(rt, ctx.bd_id);
        if (!bd) {
            node_drop(rt, NODE_BRIDGE_DOMAIN_INPUT, in->pkts[i], &ctx,
                      DROP_PORT_BINDING, 1);
            continue;
        }
        ctx.vni = bd->vni;
        node_enqueue(out, NODE_INNER_L2_PARSE, in->pkts[i], &ctx);
    }
}

void inner_l2_parse_node_run(struct app_runtime *rt, const struct node_frame *in,
                             struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        if (rte_pktmbuf_data_len(in->pkts[i]) < sizeof(struct rte_ether_hdr)) {
            node_drop(rt, NODE_INNER_L2_PARSE, in->pkts[i], &ctx,
                      DROP_INVALID_INNER_ETH, 1);
            continue;
        }
        const struct rte_ether_hdr *eth =
            rte_pktmbuf_mtod(in->pkts[i], const struct rte_ether_hdr *);
        ctx.inner_src_mac = eth->src_addr;
        ctx.inner_dst_mac = eth->dst_addr;
        ctx.inner_ethertype = rte_be_to_cpu_16(eth->ether_type);
        if (rte_is_multicast_ether_addr(&ctx.inner_dst_mac)) ctx.flags |= PKT_F_BUM;
        node_enqueue(out, NODE_MAC_LEARN, in->pkts[i], &ctx);
    }
}

static int learnable_mac(const struct rte_ether_addr *mac)
{
    return !rte_is_zero_ether_addr(mac) && !rte_is_multicast_ether_addr(mac);
}

void mac_learn_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        const struct bridge_domain *bd = runtime_bd(rt, ctx.bd_id);
        if (bd->learning_enabled && learnable_mac(&ctx.inner_src_mac)) {
            enum fdb_entry_type type = ctx.ingress_type == INGRESS_ACCESS
                                       ? FDB_LOCAL_ACCESS : FDB_REMOTE_VTEP;
            int moved;
            if (fdb_learn(&rt->fdb, ctx.bd_id, &ctx.inner_src_mac, type,
                          ctx.ingress_port_id, ctx.outer_src_ip,
                          rte_get_tsc_cycles(), &moved) < 0)
                rt->graph.nodes[NODE_MAC_LEARN].stats.errors++;
        }
        node_enqueue(out, NODE_L2_LOOKUP, in->pkts[i], &ctx);
    }
}

void l2_lookup_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        const struct bridge_domain *bd = runtime_bd(rt, ctx.bd_id);
        if (ctx.flags & PKT_F_BUM) {
            ctx.l2_result = L2_BUM;
        } else {
            const struct fdb_entry *entry =
                fdb_lookup(&rt->fdb, ctx.bd_id, &ctx.inner_dst_mac);
            if (!entry) {
                ctx.l2_result = L2_BUM;
            } else if (entry->type == FDB_LOCAL_ACCESS) {
                if (entry->access_port_id == ctx.ingress_port_id) {
                    node_drop(rt, NODE_L2_LOOKUP, in->pkts[i], &ctx,
                              DROP_SAME_PORT, 0);
                    continue;
                }
                ctx.egress_port_id = entry->access_port_id;
                ctx.l2_result = L2_LOCAL_ACCESS;
            } else {
                if (ctx.ingress_type == INGRESS_VXLAN) {
                    node_drop(rt, NODE_L2_LOOKUP, in->pkts[i], &ctx,
                              DROP_SPLIT_HORIZON, 0);
                    continue;
                }
                ctx.remote_vtep_ip = entry->remote_vtep_ip;
                ctx.egress_port_id = bd->underlay_port_id;
                ctx.l2_result = L2_REMOTE_VTEP;
            }
        }

        if (ctx.l2_result == L2_BUM) {
            if (!bd->flooding_enabled) {
                node_drop(rt, NODE_L2_LOOKUP, in->pkts[i], &ctx,
                          DROP_FLOOD_DISABLED, 0);
                continue;
            }
            node_enqueue(out, NODE_BUM_REPLICATION, in->pkts[i], &ctx);
        } else if (ctx.l2_result == L2_LOCAL_ACCESS) {
            node_enqueue(out, NODE_LOCAL_ACCESS_FORWARD, in->pkts[i], &ctx);
        } else {
            node_enqueue(out, NODE_VXLAN_UCAST_ENCAP, in->pkts[i], &ctx);
        }
    }
}

void local_access_forward_node_run(struct app_runtime *rt,
                                   const struct node_frame *in,
                                   struct node_output *out)
{
    for (uint16_t i = 0; i < in->count; i++) {
        struct packet_ctx ctx = in->ctxs[i];
        if (ctx.egress_port_id == ctx.ingress_port_id ||
            ctx.egress_port_id >= RTE_MAX_ETHPORTS ||
            rt->port_bindings[ctx.egress_port_id].role != PORT_ACCESS) {
            node_drop(rt, NODE_LOCAL_ACCESS_FORWARD, in->pkts[i], &ctx,
                      DROP_SAME_PORT, 0);
            continue;
        }
        node_enqueue(out, NODE_ACCESS_TX, in->pkts[i], &ctx);
    }
}
