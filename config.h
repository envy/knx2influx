#ifndef CONFIG_H
#define CONFIG_H

#include "knxnet.h"
#include <thread>

namespace knxnet {
	class address_arr_t {
		public:
			knxnet::address_t *addrs;
			uint64_t len;
			address_arr_t() : addrs(nullptr), len(0) {}
			~address_arr_t()
			{
				if (addrs != nullptr)
					free(addrs);
			}
			void add(knxnet::address_t addr)
			{
				if (addrs == nullptr)
				{
					addrs = (knxnet::address_t *)calloc(1, sizeof(knxnet::address_t));
				}
				else
				{
					addrs = (knxnet::address_t *)realloc(addrs, (len +  1) * sizeof(knxnet::address_t));
				}
				addrs[len] = addr;
				len++;
			}
			void add(knxnet::address_arr_t *addr)
			{
				for (uint64_t i = 0; i < addr->len; ++i)
				{
					add(addr->addrs[i]);
				}
			}
		private:
	};
}

typedef struct __ga
{
	struct __ga *next;
	knxnet::address_t addr;
	knxnet::address_t *ignored_senders;
	size_t ignored_senders_len;
	char *series;
	uint16_t dpt;
	uint16_t subdpt;
	uint8_t convert_to_int;
	uint8_t convert_to_float;
	char **tags;
	size_t tags_len;
	uint8_t log_only;
} ga_t;

typedef struct __tags
{
	struct __tags *next;
	knxnet::address_t addr;
	char **tags;
	size_t tags_len;
	bool read_on_startup;
} tags_t;

typedef struct __timer
{
	struct __timer *next;
	uint64_t interval;
	std::thread *thread;
	knxnet::address_arr_t *addrs;
} knx_timer_t;

typedef struct __config
{
	char *file;
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
	knx_timer_t *timers;
} config_t;

int parse_config(config_t *config, void (*periodic_read_fkt)(knx_timer_t *timer));
knxnet::address_arr_t *parse_pa(const char *pa);
knxnet::address_arr_t *parse_ga(const char *ga);
knxnet::address_arr_t *parse_addr(const char *addr, const char *sep, uint8_t area_max, uint8_t line_max);
void print_config(config_t *config);

#endif
