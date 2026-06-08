#pragma once
#ifndef DPDK_PORTS_H
#define DPDK_PORTS_H
#include <stdint.h>
#include <rte_mempool.h>
#include "app.h"
#include "neigh_t.h"

struct if_state {
    uint16_t port_id;
    struct rte_ether_addr mac;
    uint32_t ip_be;
    uint16_t txq;
    struct rte_mempool *mbuf_pool;
    struct neigh_table table;
};

struct app_runtime {
    const struct app_config *conf;
    struct if_state ports[APP_MAX_PORTS];
    uint16_t port_ids[APP_MAX_PORTS];
    uint16_t port_count;
};

int vdev_create(const char *progname, const struct app_config *conf);
int app_init(const char *progname,
                  const struct app_config *conf,
                  struct app_runtime *rt);
int ports_configure(struct if_state *ifs,
                    uint16_t port_id,
                    uint16_t rx_desc,
                    uint16_t tx_desc,
                    uint32_t mbufs,
                    uint32_t cache);
void port_print_info(uint16_t port_id, const char* tag);

#endif
