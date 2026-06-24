#pragma once
#ifndef NODES_INTERNAL_H
#define NODES_INTERNAL_H

#include "runtime.h"

static inline void node_drop(struct app_runtime *rt, enum node_id node,
                             struct rte_mbuf *m, struct packet_ctx *ctx,
                             enum drop_reason reason, int error)
{
    graph_drop(rt, node, m, ctx, reason, error);
}

void drop_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void access_rx_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void underlay_rx_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void eth_input_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void arp_input_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void ipv4_input_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void udp_input_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void vxlan_validate_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void vxlan_decap_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void vni_lookup_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void access_bind_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void bridge_domain_input_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void inner_l2_parse_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void mac_learn_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void l2_lookup_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void local_access_forward_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void bum_replication_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void vxlan_ucast_encap_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void vxlan_bum_encap_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void underlay_neigh_lookup_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void underlay_rewrite_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void access_tx_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);
void underlay_tx_node_run(struct app_runtime *, const struct node_frame *, struct node_output *);

#endif
