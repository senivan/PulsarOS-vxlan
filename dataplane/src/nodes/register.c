#include "internal.h"

static void register_node(struct app_runtime *rt, enum node_id id,
                          const char *name, node_run_t run)
{
    rt->graph.nodes[id] = (struct dp_node){ .id = id, .name = name, .run = run };
}

void nodes_register(struct app_runtime *rt)
{
    register_node(rt, NODE_DROP, "drop", drop_node_run);
    register_node(rt, NODE_ACCESS_RX, "access_rx", access_rx_node_run);
    register_node(rt, NODE_UNDERLAY_RX, "underlay_rx", underlay_rx_node_run);
    register_node(rt, NODE_ETH_INPUT, "eth_input", eth_input_node_run);
    register_node(rt, NODE_ARP_INPUT, "arp_input", arp_input_node_run);
    register_node(rt, NODE_IPV4_INPUT, "ipv4_input", ipv4_input_node_run);
    register_node(rt, NODE_UDP_INPUT, "udp_input", udp_input_node_run);
    register_node(rt, NODE_ACCESS_BIND, "access_bind", access_bind_node_run);
    register_node(rt, NODE_VXLAN_VALIDATE, "vxlan_validate", vxlan_validate_node_run);
    register_node(rt, NODE_VXLAN_DECAP, "vxlan_decap", vxlan_decap_node_run);
    register_node(rt, NODE_VNI_LOOKUP, "vni_lookup", vni_lookup_node_run);
    register_node(rt, NODE_BRIDGE_DOMAIN_INPUT, "bridge_domain_input",
                  bridge_domain_input_node_run);
    register_node(rt, NODE_INNER_L2_PARSE, "inner_l2_parse", inner_l2_parse_node_run);
    register_node(rt, NODE_MAC_LEARN, "mac_learn", mac_learn_node_run);
    register_node(rt, NODE_L2_LOOKUP, "l2_lookup", l2_lookup_node_run);
    register_node(rt, NODE_LOCAL_ACCESS_FORWARD, "local_access_forward",
                  local_access_forward_node_run);
    register_node(rt, NODE_VXLAN_UCAST_ENCAP, "vxlan_ucast_encap",
                  vxlan_ucast_encap_node_run);
    register_node(rt, NODE_VXLAN_BUM_ENCAP, "vxlan_bum_encap",
                  vxlan_bum_encap_node_run);
    register_node(rt, NODE_BUM_REPLICATION, "bum_replication",
                  bum_replication_node_run);
    register_node(rt, NODE_UNDERLAY_NEIGH_LOOKUP, "underlay_neigh_lookup",
                  underlay_neigh_lookup_node_run);
    register_node(rt, NODE_UNDERLAY_REWRITE, "underlay_rewrite",
                  underlay_rewrite_node_run);
    register_node(rt, NODE_ACCESS_TX, "access_tx", access_tx_node_run);
    register_node(rt, NODE_UNDERLAY_TX, "underlay_tx", underlay_tx_node_run);
}
