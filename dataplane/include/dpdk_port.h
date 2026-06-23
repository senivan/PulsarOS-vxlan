#pragma once
#ifndef DPDK_PORTS_H
#define DPDK_PORTS_H

#include "app.h"
#include "runtime.h"

int vdev_create(const char *progname, const struct app_config *conf);
int ports_init(const struct app_config *conf, struct app_runtime *rt);
void ports_fini(struct app_runtime *rt);

#endif
