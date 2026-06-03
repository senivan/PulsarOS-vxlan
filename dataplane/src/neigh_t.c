#include "neigh_t.h"
#include <arpa/inet.h>
#include <string.h>

static inline uint32_t hash(uint32_t x){
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void neigh_learn(struct neigh_table *t, uint32_t ip_be, const struct rte_ether_addr *mac){
    uint32_t k = ip_be;
    uint32_t idx = hash(k) % NEIGH_SIZE;
    for (uint32_t i = 0; i < NEIGH_SIZE; ++i){
        uint32_t p = (idx + i) % NEIGH_SIZE;
        struct neigh_entry *e = &t->slots[p];

        if (!e->in_use){
            e->in_use = 1;
            e->ip_be = k;
            e->mac = *mac;
            return;
        }
        if (e->ip_be == k){
            if (memcmp(e->mac.addr_bytes, mac->addr_bytes, RTE_ETHER_ADDR_LEN) != 0){
                e->mac = *mac;
            }
            return;
        }
    }
}

int neigh_lookup(const struct neigh_table *t, uint32_t ip_be, struct rte_ether_addr *mac_out){
    uint32_t k = ip_be;
    uint32_t idx = hash(k) % NEIGH_SIZE;

    for (uint32_t i = 0; i < NEIGH_SIZE; ++i){
        uint32_t p = (idx + i) % NEIGH_SIZE;
        const struct neigh_entry *e = &t->slots[p];
        if (!e->in_use) return 0;
        if (e->ip_be == k){
            if (mac_out) *mac_out = e->mac;
            return 1;
        }
    }
    return 0;
}

void neigh_dump(const struct neigh_table *t, const char *tag){
    (void)t;
    (void)tag;
}
