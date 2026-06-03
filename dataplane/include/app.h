#pragma once
#ifndef APP_H
#define APP_H
#include <stdint.h>
#include "constants.h"

struct interface {
    char name[IF_NAME_MAX_LEN];
    char pcie_addr[16];
    uint32_t ip_addr;
};

struct app_config {
    enum {
        PMD_TAP,
        PMD_AFPKT,
        PMD_PHYS
    } pmd;
    struct interface underlay;
    uint64_t lcore_mask;
};

#endif
