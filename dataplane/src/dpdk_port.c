#include <stdio.h>
#include <string.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include "constants.h"
#include "dpdk_port.h"

#define EAL_ARG_MAX 128
#define EAL_ARG_LEN 128

static const char *role_name(enum app_port_role role)
{
    return role == PORT_UNDERLAY ? "underlay" : "access";
}

static int add_arg(char *argv[],
                   char storage[][EAL_ARG_LEN],
                   int *argc,
                   const char *arg)
{
    if (*argc >= EAL_ARG_MAX) return -1;
    snprintf(storage[*argc], EAL_ARG_LEN, "%s", arg);
    argv[*argc] = storage[*argc];
    (*argc)++;
    return 0;
}

static int add_eal_port_args(char *argv[],
                             char storage[][EAL_ARG_LEN],
                             int *argc,
                             const struct app_config *conf)
{
    uint16_t i;
    unsigned tap_id = 0;
    unsigned afpkt_id = 0;

    for (i = 0; i < conf->port_count; i++) {
        const struct app_port_config *port = &conf->ports[i];
        char arg[EAL_ARG_LEN];

        if (port->pmd == PMD_TAP) {
            snprintf(arg, sizeof(arg), "--vdev=net_tap%u,iface=%s", tap_id++, port->iface);
            if (add_arg(argv, storage, argc, arg) < 0) return -1;
        } else if (port->pmd == PMD_AFPKT) {
            snprintf(arg, sizeof(arg), "--vdev=net_af_packet%u,iface=%s", afpkt_id++, port->iface);
            if (add_arg(argv, storage, argc, arg) < 0) return -1;
        } else if (port->pmd == PMD_PHYS) {
            if (add_arg(argv, storage, argc, "-a") < 0) return -1;
            if (add_arg(argv, storage, argc, port->pci_addr) < 0) return -1;
        }
    }

    if (conf->port_count == 0) {
        char arg[EAL_ARG_LEN];

        if (conf->pmd == PMD_TAP) {
            snprintf(arg, sizeof(arg), "--vdev=net_tap0,iface=%s", conf->underlay.name);
            if (add_arg(argv, storage, argc, arg) < 0) return -1;
        } else if (conf->pmd == PMD_AFPKT) {
            snprintf(arg, sizeof(arg), "--vdev=net_af_packet0,iface=%s", conf->underlay.name);
            if (add_arg(argv, storage, argc, arg) < 0) return -1;
        } else if (conf->pmd == PMD_PHYS) {
            if (add_arg(argv, storage, argc, "-a") < 0) return -1;
            if (add_arg(argv, storage, argc, conf->underlay.pcie_addr) < 0) return -1;
        }
    }

    return 0;
}

int vdev_create(const char* progname, const struct app_config *conf)
{
    char storage[EAL_ARG_MAX][EAL_ARG_LEN];
    char *argv[EAL_ARG_MAX];
    int argc = 0;

    if (add_arg(argv, storage, &argc, progname) < 0) return -1;
    if (add_arg(argv, storage, &argc, "-l") < 0) return -1;
    if (add_arg(argv, storage, &argc, conf->lcores) < 0) return -1;
    if (add_arg(argv, storage, &argc, "-n") < 0) return -1;
    if (add_arg(argv, storage, &argc, "1") < 0) return -1;
    if (add_arg(argv, storage, &argc, "--proc-type=auto") < 0) return -1;
    if (add_eal_port_args(argv, storage, &argc, conf) < 0) return -1;

    int rc = rte_eal_init(argc, argv);
    if (rc < 0) return -1;
    return 0;
}

static int create_pool(struct if_state *handle, uint32_t mbufs, uint32_t cache){
    char name[32];
    sprintf(name, "mbufs_%d",handle->port_id);
    handle->mbuf_pool = rte_pktmbuf_pool_create(name,
                                                mbufs,
                                                cache,
                                                0,
                                                RTE_MBUF_DEFAULT_BUF_SIZE,
                                                rte_socket_id());
    if(!handle->mbuf_pool) return -1;
    return 0;
}

void port_print_info(uint16_t port_id, const char* tag){
    struct rte_ether_addr mac;
    rte_eth_macaddr_get(port_id, &mac);
    struct rte_eth_link link;
    int rc = rte_eth_link_get_nowait(port_id, &link);
    if (rc < 0) memset(&link, 0, sizeof(link));
    printf("[%s] port %u: MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s %u Mbps\n",
        tag, port_id,
        mac.addr_bytes[0], mac.addr_bytes[1], mac.addr_bytes[2],
        mac.addr_bytes[3], mac.addr_bytes[4], mac.addr_bytes[5],
        link.link_status ? "UP" : "DOWN", link.link_speed);
}

int ports_configure(struct if_state *ifs,
                    uint16_t port_id,
                    uint16_t rx_desc,
                    uint16_t tx_desc,
                    uint32_t mbufs,
                    uint32_t cache)
{
    ifs->port_id = port_id;
    ifs->txq = 0;

    if(create_pool(ifs, mbufs, cache) < 0) return -1;

    struct rte_eth_conf conf;
    memset(&conf, 0, sizeof(conf));
    conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    conf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

    int rc = rte_eth_dev_configure(ifs->port_id, 1, 1, &conf);
    if (rc < 0) return rc;
    rc = rte_eth_rx_queue_setup(ifs->port_id, 0, rx_desc, rte_eth_dev_socket_id(ifs->port_id), NULL, ifs->mbuf_pool);
    if (rc < 0) return rc;
    rc = rte_eth_tx_queue_setup(ifs->port_id, 0, tx_desc, rte_eth_dev_socket_id(ifs->port_id), NULL);
    if (rc < 0) return rc;
    rc = rte_eth_dev_start(ifs->port_id);
    if (rc < 0) return rc;
    rte_eth_promiscuous_enable(ifs->port_id);

    rte_eth_macaddr_get(ifs->port_id, &ifs->mac);
    neigh_init(&ifs->table);

    return 0;
}

static int resolve_config_port_id(const struct app_config *conf,
                                  uint16_t config_index,
                                  uint16_t *port_id)
{
    const struct app_port_config *port = &conf->ports[config_index];
    char name[64];
    unsigned same_pmd_index = 0;
    uint16_t i;

    for (i = 0; i < config_index; i++) {
        if (conf->ports[i].pmd == port->pmd) same_pmd_index++;
    }

    if (port->pmd == PMD_TAP) {
        snprintf(name, sizeof(name), "net_tap%u", same_pmd_index);
    } else if (port->pmd == PMD_AFPKT) {
        snprintf(name, sizeof(name), "net_af_packet%u", same_pmd_index);
    } else {
        snprintf(name, sizeof(name), "%s", port->pci_addr);
    }

    if (rte_eth_dev_get_port_by_name(name, port_id) == 0) return 0;

    if (port->pmd != PMD_PHYS) {
        fprintf(stderr, "failed to resolve DPDK port %s for config port %s\n",
                name, port->name);
        return -1;
    }

    fprintf(stderr, "failed to resolve physical DPDK port %s for config port %s\n",
            port->pci_addr, port->name);
    return -1;
}

int app_init(const char *progname,
                  const struct app_config *conf,
                  struct app_runtime *rt)
{
    uint16_t i;

    if (!progname || !conf || !rt) return -1;
    memset(rt, 0, sizeof(*rt));
    rt->conf = conf;
    rt->port_count = conf->port_count;

    if (vdev_create(progname, conf) < 0) {
        fprintf(stderr, "failed to initialize DPDK EAL\n");
        return -1;
    }

    for (i = 0; i < conf->port_count; i++) {
        const struct app_port_config *port = &conf->ports[i];
        uint16_t port_id;

        if (resolve_config_port_id(conf, i, &port_id) < 0) return -1;
        rt->port_ids[i] = port_id;
        rt->ports[i].ip_be = port->has_ip ? port->ip_be : 0;

        int rc = ports_configure(&rt->ports[i],
                                 port_id,
                                 conf->rx_desc,
                                 conf->tx_desc,
                                 conf->mbufs,
                                 conf->mbuf_cache);
        if (rc < 0) {
            fprintf(stderr, "failed to configure port %s DPDK port %u: %d\n",
                    port->name, port_id, rc);
            return -1;
        }

        printf("configured %s port %s as DPDK port %u\n",
               role_name(port->role), port->name, port_id);
        port_print_info(port_id, port->name);
    }

    return 0;
}
