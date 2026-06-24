#pragma once
#ifndef RUNTIME_H
#define RUNTIME_H

#include <signal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include "app.h"
#include "bridge_domain.h"
#include "fdb.h"
#include "graph.h"
#include "neighbor.h"

struct vni_entry { uint32_t vni; uint16_t bd_id; uint8_t in_use; };
struct port_binding { uint16_t bd_id; uint8_t configured; uint8_t role; };
struct port_state {
    uint16_t port_id;
    uint16_t mtu;
    uint32_t ip_be;
    uint8_t prefix_len;
    uint8_t role;
    struct rte_ether_addr mac;
};

struct app_runtime {
    struct rte_mempool *mbuf_pool;
    struct port_state ports[APP_MAX_PORTS];
    struct port_binding port_bindings[RTE_MAX_ETHPORTS];
    uint16_t port_count;
    uint16_t vxlan_udp_port;
    struct bridge_domain bds[APP_MAX_BRIDGE_DOMAINS + 1];
    uint16_t bd_count;
    struct vni_entry vni_table[VNI_TABLE_SIZE];
    struct fdb_table fdb;
    struct neigh_table neigh;
    struct dp_graph graph;
    struct node_output *active_output;
    volatile sig_atomic_t stop;
};

int app_init(const char *progname, const struct app_config *conf, struct app_runtime *rt);
int app_run(struct app_runtime *rt);
void app_fini(struct app_runtime *rt);
void app_dump_stats(const struct app_runtime *rt);

struct port_state *runtime_port(struct app_runtime *rt, uint16_t port_id);
int runtime_vni_insert(struct app_runtime *rt, uint32_t vni, uint16_t bd_id);

#endif
