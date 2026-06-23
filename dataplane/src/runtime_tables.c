#include <string.h>
#include "runtime.h"

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; return x ^ (x >> 16);
}

const struct bridge_domain *runtime_bd(const struct app_runtime *rt, uint16_t bd_id)
{
    return bd_id && bd_id <= rt->bd_count ? &rt->bds[bd_id] : NULL;
}

const struct bridge_domain *runtime_vni_lookup(const struct app_runtime *rt, uint32_t vni)
{
    uint32_t start = hash32(vni) & (VNI_TABLE_SIZE - 1);
    for (uint32_t i = 0; i < VNI_TABLE_SIZE; i++) {
        const struct vni_entry *e = &rt->vni_table[(start + i) & (VNI_TABLE_SIZE - 1)];
        if (!e->in_use) return NULL;
        if (e->vni == vni) return runtime_bd(rt, e->bd_id);
    }
    return NULL;
}

int runtime_peer_allowed(const struct bridge_domain *bd, uint32_t ip_be)
{
    for (uint16_t i = 0; i < bd->remote_vtep_count; i++)
        if (bd->remote_vteps[i] == ip_be) return 1;
    return 0;
}

struct port_state *runtime_port(struct app_runtime *rt, uint16_t port_id)
{
    for (uint16_t i = 0; i < rt->port_count; i++) if (rt->ports[i].port_id == port_id) return &rt->ports[i];
    return NULL;
}

static uint32_t neigh_hash(uint16_t port, uint32_t ip) { return hash32(ip ^ ((uint32_t)port << 16)); }

static struct neigh_entry *neigh_find(struct neigh_table *table, uint16_t port_id,
                                      uint32_t ip_be, int create)
{
    uint32_t start = neigh_hash(port_id, ip_be) & (NEIGH_SIZE - 1);
    for (uint32_t i = 0; i < NEIGH_SIZE; i++) {
        struct neigh_entry *e = &table->slots[(start + i) & (NEIGH_SIZE - 1)];
        if (e->state == NEIGH_EMPTY) {
            if (!create) return NULL;
            e->ip_be = ip_be; e->egress_port_id = port_id; e->state = NEIGH_INCOMPLETE;
            return e;
        }
        if (e->ip_be == ip_be && e->egress_port_id == port_id) return e;
    }
    return NULL;
}

const struct neigh_entry *neigh_lookup(const struct neigh_table *table, uint16_t port_id, uint32_t ip_be)
{
    uint32_t start = neigh_hash(port_id, ip_be) & (NEIGH_SIZE - 1);
    for (uint32_t i = 0; i < NEIGH_SIZE; i++) {
        const struct neigh_entry *e = &table->slots[(start + i) & (NEIGH_SIZE - 1)];
        if (e->state == NEIGH_EMPTY) return NULL;
        if (e->ip_be == ip_be && e->egress_port_id == port_id) return e;
    }
    return NULL;
}

int neigh_learn(struct neigh_table *table, uint16_t port_id, uint32_t ip_be,
                const struct rte_ether_addr *mac, uint64_t now)
{
    struct neigh_entry *e = neigh_find(table, port_id, ip_be, 1);
    if (!e) return -1;
    e->mac = *mac; e->state = NEIGH_REACHABLE; e->last_seen_tsc = now;
    return 0;
}

int neigh_mark_request(struct neigh_table *table, uint16_t port_id, uint32_t ip_be,
                       uint64_t now, uint64_t retry_ticks)
{
    struct neigh_entry *e = neigh_find(table, port_id, ip_be, 1);
    if (!e) return -1;
    if (e->last_request_tsc && now - e->last_request_tsc < retry_ticks) return 0;
    e->last_request_tsc = now;
    return 1;
}
