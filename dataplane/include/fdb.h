#pragma once
#ifndef FDB_H
#define FDB_H

#include <stdint.h>
#include <rte_ether.h>
#include "constants.h"

enum fdb_entry_type { FDB_LOCAL_ACCESS, FDB_REMOTE_VTEP };
struct fdb_entry {
    struct rte_ether_addr mac;
    uint16_t bd_id;
    uint16_t access_port_id;
    uint32_t remote_vtep_ip;
    uint64_t last_seen_tsc;
    uint8_t type;
    uint8_t in_use;
};
struct fdb_table { struct fdb_entry slots[FDB_SIZE]; };

void fdb_init(struct fdb_table *table);
int fdb_learn(struct fdb_table *table, uint16_t bd_id,
              const struct rte_ether_addr *mac, enum fdb_entry_type type,
              uint16_t access_port_id, uint32_t remote_vtep_ip, uint64_t now,
              int *moved);
const struct fdb_entry *fdb_lookup(const struct fdb_table *table, uint16_t bd_id,
                                   const struct rte_ether_addr *mac);

#endif
