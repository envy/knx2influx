#ifndef CONFIG_H
#define CONFIG_H

#include "knx.h"

int parse_config(config_t *config);
knxnet::address_t *parse_pa(char *pa);
knxnet::address_t *parse_ga(char *ga);
knxnet::address_t *parse_addr(char *addr, char *sep, uint8_t area_max, uint8_t line_max);
void print_config(config_t *config);

#endif
