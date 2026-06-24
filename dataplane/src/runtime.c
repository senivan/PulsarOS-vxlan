#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_pause.h>
#include "dpdk_port.h"
#include "vxlan.h"

int app_init(const char *progname, const struct app_config *conf, struct app_runtime *rt)
{
    memset(rt, 0, sizeof(*rt)); rt->vxlan_udp_port = conf->vxlan_udp_port;
    if (vdev_create(progname, conf) < 0) return -1;
    if (ports_init(conf, rt) < 0) { ports_fini(rt); rte_eal_cleanup(); return -1; }
    fdb_init(&rt->fdb); rt->bd_count = conf->segment_count;
    for (uint16_t i = 0; i < conf->segment_count; i++) {
        const struct app_vxlan_segment *s = &conf->segments[i]; struct bridge_domain *bd = &rt->bds[i + 1];
        bd->bd_id = i + 1; bd->vni = s->vni; bd->learning_enabled = s->learning; bd->flooding_enabled = s->flooding;
        bd->underlay_port_id = rt->ports[s->underlay_port].port_id;
        bd->access_port_count = s->access_port_count; bd->remote_vtep_count = s->peer_count;
        for (uint16_t p = 0; p < s->access_port_count; p++) {
            uint16_t port_id = rt->ports[s->access_ports[p]].port_id;
            bd->access_ports[p] = port_id; rt->port_bindings[port_id].bd_id = bd->bd_id;
        }
        for (uint16_t p = 0; p < s->peer_count; p++) bd->remote_vteps[p] = s->peers[p].ip_be;
        if (runtime_vni_insert(rt, bd->vni, bd->bd_id) < 0) { app_fini(rt); return -1; }
    }
    if (graph_init(rt) < 0) { app_fini(rt); return -1; }
    return 0;
}

int app_run(struct app_runtime *rt)
{
    for (uint16_t p = 0; p < rt->port_count; p++) if (rt->ports[p].role == PORT_UNDERLAY) {
        struct rte_mbuf *m = arp_build_request(rt, rt->ports[p].port_id, rt->ports[p].ip_be);
        if (m) { struct node_frame f = { .count = 1 }; f.pkts[0] = m; f.ctxs[0].egress_port_id = rt->ports[p].port_id;
            graph_submit(rt, NODE_UNDERLAY_TX, &f); }
    }
    while (!rt->stop) {
        unsigned work = 0;
        for (uint16_t p = 0; p < rt->port_count; p++) {
            struct node_frame frame; memset(&frame, 0, sizeof(frame));
            frame.count = rte_eth_rx_burst(rt->ports[p].port_id, 0, frame.pkts, GRAPH_FRAME_SIZE);
            if (!frame.count) continue;
            work += frame.count;
            for (uint16_t i = 0; i < frame.count; i++) frame.ctxs[i].ingress_port_id = rt->ports[p].port_id;
            graph_submit(rt, rt->ports[p].role == PORT_ACCESS ? NODE_ACCESS_RX : NODE_UNDERLAY_RX, &frame);
        }
        if (!work) rte_pause();
    }
    return 0;
}

void app_dump_stats(const struct app_runtime *rt) { graph_dump_stats(rt); }
void app_fini(struct app_runtime *rt) { ports_fini(rt); rte_eal_cleanup(); }
