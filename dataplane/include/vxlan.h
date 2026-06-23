#pragma once
#ifndef VXLAN_H
#define VXLAN_H

#include <stdint.h>
#include <rte_mbuf.h>
#include "graph.h"

struct app_runtime;
int vxlan_encapsulate(struct app_runtime *rt, struct rte_mbuf *m,
                      struct packet_ctx *ctx);
struct rte_mbuf *arp_build_request(struct app_runtime *rt, uint16_t port_id,
                                   uint32_t target_ip_be);

#endif
