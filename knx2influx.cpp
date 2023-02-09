#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <pthread.h>

#include <chrono>

#include <curl/curl.h>

#include <sstream>

#include "knx.h"
#include "config.h"

#include "knxnet.h"

#define MULTICAST_PORT            3671 // [Default 3671]
#define MULTICAST_IP              "224.0.23.12" // [Default IPAddress(224, 0, 23, 12)]


///////////////////////////////////////////////////////////////////////////////

static config_t config;
static volatile uint8_t notifier;
static pthread_rwlock_t periodic_barrier;
static knxnet::KNXnet *knx = nullptr;
static CURL *handle = nullptr;

void exithandler()
{
	delete knx;
}

size_t curl_write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
   return size * nmemb;
}

static std::string address_to_string(knxnet::address_t &a, char delim)
{
	std::stringstream ss;
	switch (delim)
	{
		case '.':
			ss << (int)a.pa.area;
			ss << '.';
			ss << (int)a.pa.line;
			ss << '.';
			ss << (int)a.pa.member;
			break;
		case '/':
			ss << (int)a.ga.area;
			ss << '/';
			ss << (int)a.ga.line;
			ss << '/';
			ss << (int)a.ga.member;
			break;
	}
	std::string s = ss.str();
	return s;
}

static void post(char const *data)
{
	if (config.dryrun)
	{
		return;
	}

	CURLcode ret;

	if (handle == nullptr)
	{
		handle = curl_easy_init();
		char host[1024];
		host[0] = 0;
		strcat(host, config.host);
		strcat(host, "/write?consistency=any&db=");
		strcat(host, config.database);
		curl_easy_setopt(handle, CURLOPT_URL, host);
		curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);
		curl_easy_setopt(handle, CURLOPT_HEADER, 1L);
		curl_easy_setopt(handle, CURLOPT_USERAGENT, "knx2influx");
		curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 50L);
		curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "POST");
		curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_data);
	}

	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(data));

	ret = curl_easy_perform(handle);
	if (ret != CURLE_OK)
	{
		std::cerr << "Error doing curl request" << std::endl;
		return;
	}
	long http_code = 0;
	ret = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
	if (ret != CURLE_OK)
	{
		std::cerr << "Error getting HTTP status code" << std::endl;
		return;
	}
	if (http_code < 200 || http_code > 299)
	{
		std::cerr << "HTTP error: " << http_code << std::endl;
		return;
	}
}

static void format_dpt(ga_t *entry, char *_post, uint8_t *data)
{
	switch (entry->dpt)
	{
		case 1:
		{
			bool val = knxnet::data_to_bool(data);
			strcat(_post, "value=");
			strcat(_post, val ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			break;
		}
		case 2:
		{
			bool val = knxnet::data_to_bool(data);
			strcat(_post, "value=");
			strcat(_post, val ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			uint8_t other_bit = data[0] >> 1;
			bool control = knxnet::data_to_bool(&other_bit);
			strcat(_post, ",control=");
			strcat(_post, control ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			break;
		}
		case 5:
		{
			uint8_t val = knxnet::data_to_1byte_uint(data);
			switch (entry->subdpt)
			{
				case 0: // If subdpt is 0, then no was given. Assume 1 in this case
				case 1:
				{
					strcat(_post, "value=");
					if (entry->convert_to_int)
					{
						char buf[4];
						snprintf(buf, 4, "%u", (int)((val/255.0f)*100.0f));
						strcat(_post, buf);
					}
					else
					{
						char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
						snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", (val/255.0f)*100.0f);
						strcat(_post, buf);
					}
					break;
				}
				case 3:
				{
					strcat(_post, "value=");
					if (entry->convert_to_int)
					{
						char buf[4];
						snprintf(buf, 4, "%u", (int)((val/255.0f)*360.0f));
						strcat(_post, buf);
					}
					else
					{
						char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
						snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", (val/255.0f)*360.0f);
						strcat(_post, buf);
					}
					break;
				}
				case 4:
				{
					char buf[4];
					snprintf(buf, 4, "%u", val);
					strcat(_post, "value=");
					strcat(_post, buf);
					strcat(_post, "i");
					break;
				}
			}
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
			uint8_t len = (entry->convert_to_float ? 12 : 11);
			char buf[len];
			if (entry->convert_to_float)
			{
				snprintf(buf, len, "%u.0", val);
				strcat(_post, "value=");
				strcat(_post, buf);
			}
			else
			{
				snprintf(buf, len, "%u", val);
				strcat(_post, "value=");
				strcat(_post, buf);
				strcat(_post, "i");
			}
			break;
		}
		case 13:
		{
			int32_t val = knxnet::data_to_4byte_int(data);
			uint8_t len = (entry->convert_to_float ? 13 : 12);
			char buf[len];
			if (entry->convert_to_float)
			{
				snprintf(buf, len, "%d.0", val);
				strcat(_post, "value=");
				strcat(_post, buf);
			}
			else
			{
				snprintf(buf, len, "%d", val);
				strcat(_post, "value=");
				strcat(_post, buf);
				strcat(_post, "i");
			}
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
		case 22:
		{
			typedef union {
				uint8_t b8[2];
				struct {
					uint8_t reserved:1;
					uint8_t overheatAlarm:1;
					uint8_t frostAlarm:1;
					uint8_t dewPointStatus:1;
					uint8_t coolingDisabled:1;
					uint8_t statusPreCool:1;
					uint8_t statusEcoC:1;
					uint8_t heatCoolMode:1;
					uint8_t heatingDisabled:1;
					uint8_t statusStopOptim:1;
					uint8_t statusStartOptim:1;
					uint8_t statusMorningBoostH:1;
					uint8_t tempReturnLimit:1;
					uint8_t tempFlowLimit:1;
					uint8_t statusEcoH:1;
					uint8_t fault:1;
				} fields;
			} rhcc_t;
			switch (entry->subdpt)
			{
				case 0: // If subdpt is 0, then no was given. Assume 101 in this case
				case 101:
				{
					rhcc_t d;
					d.b8[0] = data[2];
					d.b8[1] = data[1];
					strcat(_post, "overheatAlarm=");
					strcat(_post, d.fields.overheatAlarm ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",frostAlarm=");
					strcat(_post, d.fields.frostAlarm ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",dewPointStatus=");
					strcat(_post, d.fields.dewPointStatus ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",coolingDisabled=");
					strcat(_post, d.fields.coolingDisabled ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusPreCool=");
					strcat(_post, d.fields.statusPreCool ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusEcoC=");
					strcat(_post, d.fields.statusEcoC ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",heatCoolMode=");
					strcat(_post, d.fields.heatCoolMode ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",heatingDisabled=");
					strcat(_post, d.fields.heatingDisabled ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusStopOptim=");
					strcat(_post, d.fields.statusStopOptim ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusStartOptim=");
					strcat(_post, d.fields.statusStartOptim ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusMorningBoostH=");
					strcat(_post, d.fields.statusMorningBoostH ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",tempReturnLimit=");
					strcat(_post, d.fields.tempReturnLimit ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",tempFlowLimit=");
					strcat(_post, d.fields.tempFlowLimit ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",statusEcoH=");
					strcat(_post, d.fields.statusEcoH ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					strcat(_post, ",fault=");
					strcat(_post, d.fields.fault ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
					break;
				}
			}
			break;
		}
		case 232:
		{
			// we assume that DPT 232.600 is meant
			uint8_t red = data[1];
			uint8_t green = data[2];
			uint8_t blue = data[3];
			char buf[4];
			snprintf(buf, 4, "%u", red);
			strcat(_post, "red=");
			strcat(_post, buf);
			snprintf(buf, 4, "%u", green);
			strcat(_post, ",green=");
			strcat(_post, buf);
			snprintf(buf, 4, "%u", blue);
			strcat(_post, ",blue=");
			strcat(_post, buf);
			break;
		}
		case 60000:
		{
			typedef union {
				uint8_t b8;
				struct {
					uint8_t frostAlarm:1;
					uint8_t controllerStatus:1;
					uint8_t heatCool:1;
					uint8_t dewPoint:1;
					uint8_t frostHeatProtection:1;
					uint8_t night:1;
					uint8_t standby:1;
					uint8_t comfort:1;
				} fields;
			} hvac_status_t;
			hvac_status_t d;
			d.b8 = data[1];
			strcat(_post, "frostAlarm=");
			strcat(_post, d.fields.frostAlarm ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",controllerStatus=");
			strcat(_post, d.fields.controllerStatus ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",heatCool=");
			strcat(_post, d.fields.heatCool ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",dewPoint=");
			strcat(_post, d.fields.dewPoint ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",frostHeatProtection=");
			strcat(_post, d.fields.frostHeatProtection ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",night=");
			strcat(_post, d.fields.night ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",standby=");
			strcat(_post, d.fields.standby ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			strcat(_post, ",comfort=");
			strcat(_post, d.fields.comfort ? (entry->convert_to_int ? "1" : "t") : (entry->convert_to_int ? "0" : "f"));
			break;
		}
	}
}

static void construct_request(char *buf, ga_t *entry, knxnet::address_t sender, knxnet::address_t ga, uint8_t *data)
{
	strcat(buf, entry->series);
	// Add tags, first the ones from the GA entries
	strcat(buf, ",sender=");
	char sbuf[2+1+2+1+3+1];
	snprintf(sbuf, sizeof(sbuf), "%u.%u.%u", sender.pa.area, sender.pa.line, sender.pa.member);
	strcat(buf, sbuf);
	memset(sbuf, 0, sizeof(sbuf));
	snprintf(sbuf, sizeof(sbuf), "%u/%u/%u", ga.ga.area, ga.ga.line, ga.ga.member);
	strcat(buf, ",ga=");
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

static void find_triggers(knxnet::message_t &msg)
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
				std::cout << "Ignoring sender " << address_to_string(msg.sender, '.');
				std::cout << " for << " << address_to_string(msg.receiver, '/') << std::endl;
				goto next;
			}
		}

		char _post[1024];
		memset(_post, 0, 1024);

		construct_request(_post, entry, msg.sender, msg.receiver, msg.data);

		std::cout << _post << std::endl;
		post(_post);

next:
		entry = entry->next;
	}
}

void periodic_read(knx_timer_t *timer)
{
	uint8_t buf[] = {0};
	pthread_rwlock_rdlock(&periodic_barrier);

	while(1)
	{
		std::this_thread::sleep_for(std::chrono::seconds(timer->interval));

		for (uint64_t i = 0; i < timer->addrs->len; ++i)
		{
			std::cout << "Reading from " << address_to_string(timer->addrs->addrs[i], '/') << std::endl;
			knxnet::message_t msg;
			msg.sender = config.physaddr;
			msg.receiver = timer->addrs->addrs[i];
			msg.data = buf;
			msg.data_len = 1;
			msg.ct = knxnet::KNX_CT_READ;
			knx->send(msg);
		}
	}
}

void *read_thread(void *unused)
{
	(void)unused;

	notifier = 1;

	while(1)
	{
		knx->receive(find_triggers);
		std::cout << std::flush;
	}

	return NULL;
}

int main(int argc, char **argv)
{
	// Init config
	memset(&config, 0, sizeof(config_t));
	config.file = (char *)"knx2influx.json";

	int ch;

	while ((ch = getopt(argc, argv, "hpd:c:")) != -1) {
		switch (ch) {
			case 'p':
				if (parse_config(&config, periodic_read) < 0)
				{
					std::cerr << "Error parsing JSON." << std::flush << std::endl;
					exit(EXIT_FAILURE);
				}
				print_config(&config);
				exit(EXIT_SUCCESS);
				break;
			case 'c':
				config.file = strdup(optarg);
				break;
			case 'd':
			{
				if (parse_config(&config, periodic_read) < 0)
				{
					std::cerr << "Error parsing JSON." << std::flush << std::endl;
					exit(EXIT_FAILURE);
				}
				char *sender_ga = strdup(optarg);
				char *tofree = sender_ga;
				char *sender_s = strsep(&sender_ga, "-");
				if (sender_s == NULL)
				{
					std::cerr << "Error parsing PA-GA pair" << std::flush << std::endl;
					exit(EXIT_FAILURE);
				}
				char *ga_s = strsep(&sender_ga, "-");
				if (ga_s == NULL)
				{
					std::cerr << "Error parsing PA-GA pair" << std::flush << std::endl;
					exit(EXIT_FAILURE);
				}
				config.dryrun = true;
				knxnet::address_arr_t *sender = parse_pa(sender_s);
				knxnet::address_arr_t *ga = parse_ga(ga_s);
				knxnet::message_t msg = {};
				msg.sender = sender->addrs[0];
				msg.receiver = ga->addrs[0];
				msg.data = (uint8_t*)calloc(1, 1);
				msg.data[0] = 0;
				msg.data_len = 1;
				msg.ct = knxnet::KNX_CT_WRITE;
				std::cout << "Triggers for " << address_to_string(ga->addrs[0], '/');
				std::cout << " send by " << address_to_string(sender->addrs[0], '.') << std::endl;
				find_triggers(msg);
				delete sender;
				delete ga;
				free(tofree);
				exit(EXIT_SUCCESS);
				break;
			}
			case 'h':
			{
				std::cout << "Usage: ./" << argv[0] << " [-h] [-p] [-d <1.2.3-4/5/6>] [-c <config_file_path>]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "  -h  Print this help." << std::endl;
				std::cout << "  -p  Parses the config and prints all GA, tags and types after expaning all GA wildcards." << std::endl;
				std::cout << "  -d  Parses the config and prints all mappings for a telegram from IA 1.2.3 to GA 4/5/6." << std::endl;
				std::cout << "  -c  Set the path to the config file." << std::endl;
				exit(EXIT_SUCCESS);
			}
			case '?':
			default:
				break;
		}
	}
	argc -= optind;
	argv += optind;

	pthread_rwlock_wrlock(&periodic_barrier);

	// Parse config first
	if (parse_config(&config, periodic_read) < 0)
	{
		std::cerr << "Error parsing JSON." << std::flush << std::endl;
		exit(EXIT_FAILURE);
	}

	curl_global_init(CURL_GLOBAL_ALL);

	std::cout << "Sending data to " << config.host << " database " << config.database << std::endl;

	knx = new knxnet::KNXnet(config.interface, config.physaddr);
	atexit(exithandler);

	notifier = 0;

	pthread_t read_thread_id;
	pthread_create(&read_thread_id, NULL, read_thread, NULL);

	// Normally, this should be implemented with barriers but macOS does not have pthread barriers.
	while (notifier != 1);

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
			std::cout << "Reading from " << address_to_string(addr, '/') << std::endl;
			knxnet::message_t msg;
			msg.sender = config.physaddr;
			msg.receiver = addr;
			msg.data = buf;
			msg.data_len = 1;
			msg.ct = knxnet::KNX_CT_READ;
			knx->send(msg);
		}
	}

	pthread_rwlock_unlock(&periodic_barrier);

	pthread_join(read_thread_id, NULL);

	return 0;
}

