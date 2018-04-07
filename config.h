#ifndef CONFIG_H
#define CONFIG_H

#include "knxnet.h"

typedef struct __ga
{
	struct __ga *next;
	knxnet::address_t addr;
	knxnet::address_t *ignored_senders;
	size_t ignored_senders_len;
	char *series;
	uint8_t dpt;
	uint8_t convert_dpt1_to_int;
	char **tags;
	size_t tags_len;
} ga_t;

typedef struct __tags
{
	struct __tags *next;
	knxnet::address_t addr;
	char **tags;
	size_t tags_len;
	bool read_on_startup;
} tags_t;

typedef struct __config
{
	char *interface;
	char *host;
	char *database;
	char *user;
	char *password;
	bool dryrun;
	knxnet::address_t physaddr;
	ga_t *gas[UINT16_MAX];
	tags_t *sender_tags[UINT16_MAX];
	tags_t *ga_tags[UINT16_MAX];
} config_t;

int parse_config(config_t *config);
knxnet::address_t *parse_pa(const char *pa);
knxnet::address_t *parse_ga(const char *ga);
knxnet::address_t *parse_addr(const char *addr, const char *sep, uint8_t area_max, uint8_t line_max);
void print_config(config_t *config);

#endif
