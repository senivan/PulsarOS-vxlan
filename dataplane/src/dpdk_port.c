#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include "dpdk_port.h"

#define EAL_ARG_MAX 128
#define EAL_ARG_LEN 128

static int add_arg(char *argv[], char storage[][EAL_ARG_LEN], int *argc, const char *arg)
{
    if (*argc >= EAL_ARG_MAX) return -1;
    snprintf(storage[*argc], EAL_ARG_LEN, "%s", arg); argv[*argc] = storage[*argc]; (*argc)++;
    return 0;
}

int vdev_create(const char *progname, const struct app_config *conf)
{
    char storage[EAL_ARG_MAX][EAL_ARG_LEN]; char *argv[EAL_ARG_MAX]; int argc = 0;
    int has_physical_port = 0;
    for (uint16_t i = 0; i < conf->port_count; i++)
        if (conf->ports[i].pmd == PMD_PHYS) has_physical_port = 1;
    if (add_arg(argv, storage, &argc, progname) || add_arg(argv, storage, &argc, "-l") ||
        add_arg(argv, storage, &argc, conf->lcores) || add_arg(argv, storage, &argc, "-n") ||
        add_arg(argv, storage, &argc, "1") || add_arg(argv, storage, &argc, "--proc-type=primary")) return -1;
    if (!has_physical_port && add_arg(argv, storage, &argc, "--no-pci")) return -1;
    char prefix[EAL_ARG_LEN];
    snprintf(prefix, sizeof(prefix), "--file-prefix=pulsaros-%ld", (long)getpid());
    if (add_arg(argv, storage, &argc, prefix)) return -1;
    unsigned tap = 0, afpkt = 0;
    for (uint16_t i = 0; i < conf->port_count; i++) {
        char arg[EAL_ARG_LEN]; const struct app_port_config *p = &conf->ports[i];
        if (p->pmd == PMD_TAP) {
            snprintf(arg, sizeof(arg), "--vdev=net_tap%u,iface=%s", tap++, p->iface);
            if (add_arg(argv, storage, &argc, arg)) return -1;
        } else if (p->pmd == PMD_AFPKT) {
            snprintf(arg, sizeof(arg), "--vdev=net_af_packet%u,iface=%s", afpkt++, p->iface);
            if (add_arg(argv, storage, &argc, arg)) return -1;
        } else {
            if (add_arg(argv, storage, &argc, "-a") || add_arg(argv, storage, &argc, p->pci_addr)) return -1;
        }
    }
    return rte_eal_init(argc, argv) < 0 ? -1 : 0;
}

static int resolve_port(const struct app_config *conf, uint16_t index, uint16_t *port_id)
{
    const struct app_port_config *p = &conf->ports[index]; char name[64]; unsigned same = 0;
    for (uint16_t i = 0; i < index; i++) if (conf->ports[i].pmd == p->pmd) same++;
    if (p->pmd == PMD_TAP) snprintf(name, sizeof(name), "net_tap%u", same);
    else if (p->pmd == PMD_AFPKT) snprintf(name, sizeof(name), "net_af_packet%u", same);
    else snprintf(name, sizeof(name), "%s", p->pci_addr);
    return rte_eth_dev_get_port_by_name(name, port_id);
}

int ports_init(const struct app_config *conf, struct app_runtime *rt)
{
    rt->mbuf_pool = rte_pktmbuf_pool_create("pulsaros_mbufs", conf->mbufs, conf->mbuf_cache, 0,
                                            RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!rt->mbuf_pool) {
        fprintf(stderr, "failed to create mbuf pool: %s\n", rte_strerror(rte_errno));
        return -1;
    }
    rt->port_count = 0;
    for (uint16_t i = 0; i < conf->port_count; i++) {
        uint16_t id; if (resolve_port(conf, i, &id) < 0 || id >= RTE_MAX_ETHPORTS) return -1;
        struct rte_eth_conf ec; memset(&ec, 0, sizeof(ec));
        ec.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE; ec.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
        int rc = rte_eth_dev_configure(id, 1, 1, &ec);
        if (!rc) rc = rte_eth_rx_queue_setup(id, 0, conf->rx_desc, rte_eth_dev_socket_id(id), NULL, rt->mbuf_pool);
        if (!rc) rc = rte_eth_tx_queue_setup(id, 0, conf->tx_desc, rte_eth_dev_socket_id(id), NULL);
        if (!rc) rc = rte_eth_dev_start(id);
        if (rc < 0) { fprintf(stderr, "failed to configure port %s: %d\n", conf->ports[i].name, rc); return -1; }
        rte_eth_promiscuous_enable(id);
        struct port_state *ps = &rt->ports[i];
        ps->port_id = id; ps->role = (uint8_t)conf->ports[i].role; ps->ip_be = conf->ports[i].ip_be;
        ps->prefix_len = conf->ports[i].prefix_len;
        rte_eth_macaddr_get(id, &ps->mac); rte_eth_dev_get_mtu(id, &ps->mtu);
        rt->port_bindings[id].configured = 1; rt->port_bindings[id].role = ps->role;
        rt->port_count++;
        printf("configured %s as DPDK port %u, mtu %u\n", conf->ports[i].name, id, ps->mtu);
    }
    return 0;
}

void ports_fini(struct app_runtime *rt)
{
    for (uint16_t i = 0; i < rt->port_count; i++) {
        rte_eth_dev_stop(rt->ports[i].port_id); rte_eth_dev_close(rt->ports[i].port_id);
    }
    if (rt->mbuf_pool) { rte_mempool_free(rt->mbuf_pool); rt->mbuf_pool = NULL; }
}
