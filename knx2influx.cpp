#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <pthread.h>

#include <curl/curl.h>

#include "knx.h"
#include "config.h"

#include "knxnet.h"

#define MULTICAST_PORT            3671 // [Default 3671]
#define MULTICAST_IP              "224.0.23.12" // [Default IPAddress(224, 0, 23, 12)]


///////////////////////////////////////////////////////////////////////////////

static config_t config;
static pthread_barrier_t bar;

static knxnet::KNXnet *knx = nullptr;

void exithandler()
{
	delete knx;
}

size_t curl_write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
   return size * nmemb;
}

void post(char const *data)
{
	if (config.dryrun)
	{
		return;
	}

	CURLcode ret;
	CURL *hnd;

	hnd = curl_easy_init();
	char host[1024];
	host[0] = 0;
	strcat(host, config.host);
	strcat(host, "/write?db=");
	strcat(host, config.database);
	curl_easy_setopt(hnd, CURLOPT_URL, host);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_HEADER, 1L);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(data));
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "knx2influx");
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 0L);

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, curl_write_data);

	ret = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);
	(void)ret;
}

void format_dpt(ga_t *entry, char *_post, uint8_t *data)
{
	switch (entry->dpt)
	{
		case 1:
		{
			bool val = knxnet::data_to_bool(data);
			strcat(_post, "value=");
			if (entry->convert_dpt1_to_int == 1)
			{
				strcat(_post, val ? "1" : "0");
			}
			else
			{
				strcat(_post, val ? "t" : "f");
			}
			break;
		}
		case 2:
		{
			bool val = knxnet::data_to_bool(data);
			strcat(_post, "value=");
			strcat(_post, val ? "t" : "f");
			uint8_t other_bit = data[0] >> 1;
			bool control = knxnet::data_to_bool(&other_bit);
			strcat(_post, ",control=");
			strcat(_post, control ? "t" : "f");
			break;
		}
		case 5:
		{
			uint8_t val = knxnet::data_to_1byte_uint(data);
			char buf[4];
			snprintf(buf, 4, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 6:
		{
			int8_t val = knxnet::data_to_1byte_int(data);
			char buf[5];
			snprintf(buf, 5, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 7:
		{
			uint16_t val = knxnet::data_to_2byte_uint(data);
			char buf[6];
			snprintf(buf, 6, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 8:
		{
			int16_t val = knxnet::data_to_2byte_int(data);
			char buf[7];
			snprintf(buf, 7, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 9:
		{
			float val = knxnet::data_to_2byte_float(data);
			char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
			snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			break;
		}
		case 12:
		{
			uint32_t val = knxnet::data_to_4byte_uint(data);
			char buf[11];
			snprintf(buf, 11, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 13:
		{
			int32_t val = knxnet::data_to_4byte_int(data);
			char buf[12];
			snprintf(buf, 12, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 14:
		{
			float val = knxnet::data_to_4byte_float(data);
			char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
			snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			break;
		}
	}
}

void construct_request(char *buf, ga_t *entry, knxnet::address_t sender, knxnet::address_t ga, uint8_t *data)
{
	strcat(buf, entry->series);
	// Add tags, first the ones from the GA entries
	strcat(buf, ",sender=");
	char sbuf[2+1+2+1+3+1];
	snprintf(sbuf, 2+1+2+1+3+1, "%u.%u.%u", sender.pa.area, sender.pa.line, sender.pa.member);
	strcat(buf, sbuf);
	for (size_t i = 0; i < entry->tags_len; ++i)
	{
		strcat(buf, ",");
		strcat(buf, entry->tags[i]);
	}
	// Now the sender tags
	tags_t *sender_tag = config.sender_tags[sender.value];
	while (sender_tag != NULL)
	{
		for (size_t i = 0; i < sender_tag->tags_len; ++i)
		{
			strcat(buf, ",");
			strcat(buf, sender_tag->tags[i]);
		}

		sender_tag = sender_tag->next;
	}
	// And the GA tags
	tags_t *ga_tag = config.ga_tags[ga.value];
	while (ga_tag != NULL)
	{
		for (size_t i = 0; i < ga_tag->tags_len; ++i)
		{
			strcat(buf, ",");
			strcat(buf, ga_tag->tags[i]);
		}

		ga_tag = ga_tag->next;
	}

	// Seperate tags from values
	strcat(buf, " ");

	format_dpt(entry, buf, data);
}

void find_triggers(knxnet::message_t &msg)
{
	if (msg.ct != knxnet::KNX_CT_ANSWER && msg.ct != knxnet::KNX_CT_WRITE)
	{
		return;
	}

	ga_t *entry = config.gas[msg.receiver.value];
	while(entry != NULL)
	{
		// Check if sender is blacklisted
		for (size_t i = 0; i < entry->ignored_senders_len; ++i)
		{
			knxnet::address_t a_cur = entry->ignored_senders[i];
			if (a_cur.value == msg.sender.value)
			{
				printf("Ignoring sender %u.%u.%u for %u/%u/%u\n", msg.sender.pa.area, msg.sender.pa.line, msg.sender.pa.member, msg.receiver.ga.area, msg.receiver.ga.line, msg.receiver.ga.member);
				goto next;
			}
		}

		char _post[1024];
		memset(_post, 0, 1024);

		construct_request(_post, entry, msg.sender, msg.receiver, msg.data);

		printf("%s\n", _post);
		post(_post);

next:
		entry = entry->next;
	}
}

void *read_thread(void *unused)
{
	(void)unused;

	pthread_barrier_wait(&bar);

	while(1)
	{
		knx->receive(find_triggers);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	// Init config
	memset(&config, 0, sizeof(config_t));

	// Parse config first
	if (parse_config(&config) < 0)
	{
		printf("Error parsing JSON.\n");
		exit(EXIT_FAILURE);
	}

	int ch;

	while ((ch = getopt(argc, argv, "pd:")) != -1) {
		switch (ch) {
			case 'p':
				print_config(&config);
				break;
			case 'd':
			{
				char *sender_ga = strdup(optarg);
				char *tofree = sender_ga;
				char *sender_s = strsep(&sender_ga, "-");
				if (sender_s == NULL)
				{
					printf("error parsing PA-GA pair\n");
					exit(EXIT_FAILURE);
				}
				char *ga_s = strsep(&sender_ga, "-");
				if (ga_s == NULL)
				{
					printf("error parsing PA-GA pair\n");
					exit(EXIT_FAILURE);
				}
				config.dryrun = true;
				knxnet::address_t *sender = parse_pa(sender_s);
				knxnet::address_t *ga = parse_ga(ga_s);
				knxnet::message_t msg = {};
				msg.sender = sender[0];
				msg.receiver = ga[0];
				msg.data = (uint8_t*)calloc(1, 1);
				msg.data[0] = 0;
				msg.data_len = 1;
				msg.ct = knxnet::KNX_CT_WRITE;
				printf("Triggers for %u/%u/%u send by %u.%u.%u\n", ga[0].ga.area, ga[0].ga.line, ga[0].ga.member, sender[0].pa.area, sender[0].pa.line, sender[0].pa.member);
				find_triggers(msg);
				free(sender);
				free(ga);
				free(tofree);
				exit(EXIT_SUCCESS);
				break;
			}
			case '?':
			default:
				break;
		}
	}
	argc -= optind;
	argv += optind;

	printf("Sending data to %s database %s\n", config.host, config.database);

	knx = new knxnet::KNXnet(config.interface, config.physaddr);
	atexit(exithandler);

	pthread_barrier_init(&bar, NULL, 2);

	pthread_t read_thread_id;
	pthread_create(&read_thread_id, NULL, read_thread, NULL);

	pthread_barrier_wait(&bar);

	usleep(1000);

	for (uint16_t a = 0; a < UINT16_MAX; ++a)
	{
		knxnet::address_t addr = { .value = a };
		tags_t *entry = config.ga_tags[a];
		uint8_t buf[] = {0};
		if (entry == NULL)
			continue;

		if (entry->read_on_startup)
		{
			printf("Reading from %u/%u/%u\n", addr.ga.area, addr.ga.line, addr.ga.member);
			knxnet::message_t msg;
			msg.sender = config.physaddr;
			msg.receiver = addr;
			msg.data = buf;
			msg.data_len = 1;
			msg.ct = knxnet::KNX_CT_READ;
			knx->send(msg);
		}
	}

	pthread_join(read_thread_id, NULL);

	return 0;
}

