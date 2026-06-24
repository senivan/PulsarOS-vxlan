#include "internal.h"

static void tx_common(struct app_runtime *rt, const struct node_frame *in,
                      enum node_id owner, uint8_t role)
{
    struct rte_mbuf *buckets[APP_MAX_PORTS][GRAPH_FRAME_SIZE];
    uint16_t counts[APP_MAX_PORTS] = {0};

    for (uint16_t i = 0; i < in->count; i++) {
        struct port_state *port = runtime_port(rt, in->ctxs[i].egress_port_id);
        if (!port || port->role != role) {
            struct packet_ctx ctx = in->ctxs[i];
            node_drop(rt, owner, in->pkts[i], &ctx, DROP_PORT_BINDING, 1);
            continue;
        }
        uint16_t index = (uint16_t)(port - rt->ports);
        buckets[index][counts[index]++] = in->pkts[i];
    }

    for (uint16_t p = 0; p < rt->port_count; p++) {
        if (!counts[p]) continue;
        uint16_t sent = rte_eth_tx_burst(rt->ports[p].port_id, 0,
                                         buckets[p], counts[p]);
        for (uint16_t i = sent; i < counts[p]; i++) {
            rt->graph.nodes[owner].stats.drops++;
            rt->graph.nodes[owner].stats.errors++;
            rt->graph.drop_reasons[DROP_TX_FAILED]++;
            rte_pktmbuf_free(buckets[p][i]);
        }
    }
}

void access_tx_node_run(struct app_runtime *rt, const struct node_frame *in,
                        struct node_output *out)
{
    (void)out;
    tx_common(rt, in, NODE_ACCESS_TX, PORT_ACCESS);
}

void underlay_tx_node_run(struct app_runtime *rt, const struct node_frame *in,
                          struct node_output *out)
{
    (void)out;
    tx_common(rt, in, NODE_UNDERLAY_TX, PORT_UNDERLAY);
}
