#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "app.h"

void app_config_defaults(struct app_config *conf);
int app_config_load_json(const char *path,
                         struct app_config *conf,
                         char *err,
                         size_t err_len);

#endif
