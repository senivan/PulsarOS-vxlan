#pragma once
#ifndef NEIGHBOR_H
#define NEIGHBOR_H

#include <stdint.h>
#include <rte_ether.h>
#include "constants.h"

enum neigh_state { NEIGH_EMPTY, NEIGH_INCOMPLETE, NEIGH_REACHABLE };
struct neigh_entry {
    uint32_t ip_be;
    struct rte_ether_addr mac;
    uint16_t egress_port_id;
    uint8_t state;
    uint64_t last_seen_tsc, last_request_tsc;
};
struct neigh_table { struct neigh_entry slots[NEIGH_SIZE]; };

const struct neigh_entry *neigh_lookup(const struct neigh_table *table, uint16_t port_id, uint32_t ip_be);
int neigh_learn(struct neigh_table *table, uint16_t port_id, uint32_t ip_be,
                const struct rte_ether_addr *mac, uint64_t now);
int neigh_mark_request(struct neigh_table *table, uint16_t port_id, uint32_t ip_be,
                       uint64_t now, uint64_t retry_ticks);

#endif
