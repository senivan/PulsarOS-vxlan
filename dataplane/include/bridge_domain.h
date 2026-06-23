#pragma once
#ifndef BRIDGE_DOMAIN_H
#define BRIDGE_DOMAIN_H

#include <stdint.h>
#include "constants.h"

struct app_runtime;
struct bridge_domain {
    uint16_t bd_id, underlay_port_id;
    uint32_t vni;
    uint16_t access_ports[APP_MAX_ACCESS_PORTS], access_port_count;
    uint32_t remote_vteps[APP_MAX_PEERS];
    uint16_t remote_vtep_count;
    uint8_t learning_enabled, flooding_enabled;
};

const struct bridge_domain *runtime_bd(const struct app_runtime *rt, uint16_t bd_id);
const struct bridge_domain *runtime_vni_lookup(const struct app_runtime *rt, uint32_t vni);
int runtime_peer_allowed(const struct bridge_domain *bd, uint32_t ip_be);

#endif
