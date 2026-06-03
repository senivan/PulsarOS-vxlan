#include <stdio.h>
#include <string.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include "constants.h"
#include "dpdk_port.h"

int vdev_create(const char* progname, const struct app_config *conf){
    static char v0[128];

    const char *base[] = { (char*)progname, "-l", "0-1", "-n", "1", "--proc-type=auto" };
    char *argv[16];
    int argc = 0;
    for (size_t i = 0; i < sizeof(base)/sizeof(base[0]); ++i) argv[argc++] = (char*)base[i];

    if (conf->pmd == PMD_TAP) {
        snprintf(v0, sizeof(v0), "--vdev=net_tap0,iface=%s", conf->underlay.name);
        argv[argc++] = v0;
    } else if (conf->pmd == PMD_AFPKT) {
        snprintf(v0, sizeof(v0), "--vdev=net_af_packet0,iface=%s", conf->underlay.name);
        argv[argc++] = v0;
    } else if (conf->pmd == PMD_PHYS){
        argv[argc++] = "-a"; argv[argc++] = (char*)conf->underlay.pcie_addr;
    }

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
    rte_eth_link_get_nowait(port_id, &link);
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
