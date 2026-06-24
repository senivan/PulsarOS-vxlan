#include <string.h>
#include "fdb.h"

static uint32_t mac_hash(uint16_t bd_id, const struct rte_ether_addr *mac)
{
    uint32_t h = 2166136261u ^ bd_id;
    for (unsigned i = 0; i < RTE_ETHER_ADDR_LEN; i++) h = (h ^ mac->addr_bytes[i]) * 16777619u;
    return h;
}

void fdb_init(struct fdb_table *table) { memset(table, 0, sizeof(*table)); }

int fdb_learn(struct fdb_table *table, uint16_t bd_id,
              const struct rte_ether_addr *mac, enum fdb_entry_type type,
              uint16_t access_port_id, uint32_t remote_vtep_ip, uint64_t now,
              int *moved)
{
    uint32_t start = mac_hash(bd_id, mac) & (FDB_SIZE - 1);
    if (moved) *moved = 0;
    for (uint32_t i = 0; i < FDB_SIZE; i++) {
        struct fdb_entry *e = &table->slots[(start + i) & (FDB_SIZE - 1)];
        if (!e->in_use) {
            *e = (struct fdb_entry){ .mac = *mac, .bd_id = bd_id,
                .access_port_id = access_port_id, .remote_vtep_ip = remote_vtep_ip,
                .last_seen_tsc = now, .type = (uint8_t)type, .in_use = 1 };
            return 0;
        }
        if (e->bd_id == bd_id && rte_is_same_ether_addr(&e->mac, mac)) {
            if (moved && (e->type != type || e->access_port_id != access_port_id ||
                          e->remote_vtep_ip != remote_vtep_ip)) *moved = 1;
            e->type = (uint8_t)type;
            e->access_port_id = access_port_id;
            e->remote_vtep_ip = remote_vtep_ip;
            e->last_seen_tsc = now;
            return 0;
        }
    }
    return -1;
}

const struct fdb_entry *fdb_lookup(const struct fdb_table *table, uint16_t bd_id,
                                   const struct rte_ether_addr *mac)
{
    uint32_t start = mac_hash(bd_id, mac) & (FDB_SIZE - 1);
    for (uint32_t i = 0; i < FDB_SIZE; i++) {
        const struct fdb_entry *e = &table->slots[(start + i) & (FDB_SIZE - 1)];
        if (!e->in_use) return NULL;
        if (e->bd_id == bd_id && rte_is_same_ether_addr(&e->mac, mac)) return e;
    }
    return NULL;
}
