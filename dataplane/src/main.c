#include <stdio.h>
#include <string.h>

#include "arp.h"
#include "config.h"
#include "dpdk_port.h"

static void usage(const char *progname)
{
    fprintf(stderr, "usage: %s <config.json>\n", progname);
}

int main(int argc, char **argv)
{
    struct app_config conf;
    struct app_runtime rt;
    char err[256];
    uint16_t i;

    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }

    if (app_config_load_json(argv[1], &conf, err, sizeof(err)) < 0) {
        fprintf(stderr, "failed to load config: %s\n", err);
        return 1;
    }

    if (app_init(argv[0], &conf, &rt) < 0) {
        return 1;
    }

    for (i = 0; i < conf.port_count; i++) {
        if (conf.ports[i].role == PORT_UNDERLAY && rt.ports[i].ip_be != 0) {
            arp_send_gratuitous(&rt.ports[i]);
        }
    }

    printf("initialized %u DPDK ports\n", rt.port_count);
    return 0;
}
