#ifndef CONFIG_H
#define CONFIG_H

#include "knx.h"

int parse_config(config_t *config);
address_t *parse_pa(char *pa);
address_t *parse_ga(char *ga);
address_t *parse_addr(char *addr, char *sep, uint8_t area_max, uint8_t line_max);
void print_config(config_t *config);

#endif
