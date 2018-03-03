#define _GNU_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>

#include <curl/curl.h>

#include "cJSON.h"
#include "knx.h"

#define MULTICAST_PORT            3671 // [Default 3671]
#define MULTICAST_IP              "224.0.23.12" // [Default IPAddress(224, 0, 23, 12)]
#define INTERFACE_IP              "10.1.36.13"


///////////////////////////////////////////////////////////////////////////////

typedef struct __ga
{
	struct __ga *next;
	address_t addr;
	address_t *ignored_senders;
	size_t ignored_senders_len;
	char *series;
	uint8_t dpt;
	char **tags;
	size_t tags_len;
} ga_t;

typedef struct __config
{
	char *host;
	char *database;
	char *user;
	char *password;
	ga_t *gas;
} config_t;

static config_t config;
static int socket_fd;
static struct ip_mreq command = {};

void exithandler()
{
	int loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_DROP_MEMBERSHIP): ");
	}
	close(socket_fd);
}

void post(char const *data)
{
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

	ret = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);
}

float data_to_2byte_float(uint8_t *data)
{
	uint8_t expo = (data[1] & 0b01111000) >> 3;
	int16_t mant = ((data[1] & 0b10000111) << 8) | data[2];
	return 0.01f * mant * pow(2, expo);
}

void process_packet(uint8_t *buf, size_t len)
{
	knx_ip_pkt_t *knx_pkt = (knx_ip_pkt_t *)buf;

	//printf("ST: 0x%02x\n", ntohs(knx_pkt->service_type));

	if (knx_pkt->header_len != 0x06 && knx_pkt->protocol_version != 0x10 && knx_pkt->service_type != KNX_ST_ROUTING_INDICATION)
		return;

	cemi_msg_t *cemi_msg = (cemi_msg_t *)knx_pkt->pkt_data;

	//printf("MT: 0x%02x\n", cemi_msg->message_code);

	if (cemi_msg->message_code != KNX_MT_L_DATA_IND)
		return;

	//printf("ADDI: 0x%02x\n", cemi_msg->additional_info_len);

	cemi_service_t *cemi_data = &cemi_msg->data.service_information;

	if (cemi_msg->additional_info_len > 0)
		cemi_data = (cemi_service_t *)(((uint8_t *)cemi_data) + cemi_msg->additional_info_len);

	//printf("C1: 0x%02x   C2: 0x%02x   DT: 0x%02x\n", cemi_data->control_1.byte, cemi_data->control_2.byte, cemi_data->control_2.bits.dest_addr_type);

	if (cemi_data->control_2.bits.dest_addr_type != 0x01)
		return;

	//printf("HC: 0x%02x   EFF: 0x%02x\n", cemi_data->control_2.bits.hop_count, cemi_data->control_2.bits.extended_frame_format);

	//printf("Source: 0x%02x%02x (%u.%u.%u)\n", cemi_data->source.bytes.high, cemi_data->source.bytes.low, cemi_data->source.pa.area, cemi_data->source.pa.line, cemi_data->source.pa.member);
	//printf("Dest:   0x%02x%02x (%u/%u/%u)\n", cemi_data->destination.bytes.high, cemi_data->destination.bytes.low, cemi_data->destination.ga.area, cemi_data->destination.ga.line, cemi_data->destination.ga.member);

	knx_command_type_t ct = (knx_command_type_t)(((cemi_data->data[0] & 0xC0) >> 6) | ((cemi_data->pci.apci & 0x03) << 2));

	//printf("CT: 0x%02x\n", ct);

	if (ct != KNX_CT_WRITE)
		return;

	/*
	for (int i = 0; i < cemi_data->data_len; ++i)
	{
		printf(" 0x%02x", cemi_data->data[i]);
	}
	printf("\n==\n");
	*/

	// Call callbacks
	//for (int i = 0; i < registered_callback_assignments; ++i)
	ga_t *cur = config.gas;
	while(cur != NULL)
	{
		//printf("Testing: 0x%02x%02x\n", callback_assignments[i].address.bytes.high, callback_assignments[i].address.bytes.low);
		//printf("Testing 0x%02x%02x (%u/%u/%u)\n", cur->addr.bytes.high, cur->addr.bytes.low, cur->addr.ga.area, cur->addr.ga.line, cur->addr.ga.member);
		//if (cemi_data->destination.value == callback_assignments[i].address.value)
		if (cur->addr.value == cemi_data->destination.value)
	    {
			//printf("Found match\n");
			//if (callbacks[callback_assignments[i].callback_id].cond && !callbacks[callback_assignments[i].callback_id].cond())
			for (int i = 0; i < cur->ignored_senders_len; ++i)
			{
				address_t a_cur = cur->ignored_senders[i];
				if (a_cur.value == cemi_data->source.value)
				{
					//printf("But it's disabled\n");
					goto next;
				}
			}
			uint8_t data[cemi_data->data_len];
			memcpy(data, cemi_data->data, cemi_data->data_len);
			data[0] = data[0] & 0x3F;
			message_t msg = {};
			msg.ct = ct;
			msg.received_on = cemi_data->destination;
			msg.data_len = cemi_data->data_len;
			msg.data = data;

			char _post[1024];
			memset(_post, 0, 1024);
			strcat(_post, cur->series);
			for (int i = 0; i < cur->tags_len; ++i)
			{
				strcat(_post, ",");
				strcat(_post, cur->tags[i]);
			}
			strcat(_post, " value=");
			
			switch (cur->dpt)
			{
				case 9:
				{
					float val = data_to_2byte_float(data);
					char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
					snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", val);
					strcat(_post, buf);
				}
			}

			printf("%s\n", _post);
			post(_post);
		}
next:
		cur = cur->next;
	}
	printf("\n");
}


int parse_config()
{
	int status = 0;
	FILE *f = fopen("knx2influx.json", "rb");
	fseek(f, 0, SEEK_END);
	uint64_t fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *json_str = malloc(fsize + 1);
	fread(json_str, fsize, 1, f);
	fclose(f);
	json_str[fsize] = '\0';

	char *error_ptr;
	cJSON *json = cJSON_Parse(json_str);

	if (json == NULL)
	{
		error_ptr = (char *)cJSON_GetErrorPtr();
		goto end;
	}

	cJSON *host = cJSON_GetObjectItemCaseSensitive(json, "host");
	if (cJSON_IsString(host) && (host->valuestring != NULL))
	{
		config.host = strdup(host->valuestring);
	}
	else
	{
		error_ptr = "No host given in config!";
		goto error;
	}

	cJSON *database = cJSON_GetObjectItemCaseSensitive(json, "database");
	if (cJSON_IsString(database) && (database->valuestring != NULL))
	{
		config.database = strdup(database->valuestring);
	}
	else
	{
		error_ptr = "No database given in config!";
		goto error;
	}
	cJSON *user = cJSON_GetObjectItemCaseSensitive(json, "user");
	if (cJSON_IsString(user) && (user->valuestring != NULL))
	{
		config.database = strdup(user->valuestring);
	}
	cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
	if (cJSON_IsString(password) && (password->valuestring != NULL))
	{
		config.password = strdup(password->valuestring);
	}

	// GAs
	
	cJSON *gas = cJSON_GetObjectItemCaseSensitive(json, "gas");
	if (!cJSON_IsArray(gas))
	{
		error_ptr = "Expected array, got something else for 'gas'";
		goto error;
	}
	cJSON *ga_obj = NULL;

	ga_t *prev_ga = NULL;

	cJSON_ArrayForEach(ga_obj, gas)
	{
		if (!cJSON_IsObject(ga_obj))
		{
			error_ptr = "Expected array of ojects, got something that is not object in 'gas'";
			goto error;
		}
		cJSON *ga = cJSON_GetObjectItemCaseSensitive(ga_obj, "ga");
		if (!cJSON_IsString(ga))
		{
			error_ptr = "'ga' is not a string!";
			goto error;
		}
		if (ga->valuestring == NULL)
		{
			error_ptr = "'ga' must not be empty!";
			goto error;
		}
		printf("GA: %s", ga->valuestring);

		uint32_t area, line, member;
		
		sscanf(ga->valuestring, "%u/%u/%u", &area, &line, &member);

		printf(" -> %u/%u/%u\n", area, line, member);

		cJSON *series = cJSON_GetObjectItemCaseSensitive(ga_obj, "series");
		if (!cJSON_IsString(series))
		{
			error_ptr = "'series' is not a string!";
			goto error;
		}
		if (ga->valuestring == NULL)
		{
			error_ptr = "'series' must not be empty!";
			goto error;
		}
		printf("Series: %s\n", series->valuestring);

		cJSON *dpt = cJSON_GetObjectItemCaseSensitive(ga_obj, "dpt");
		if (!cJSON_IsNumber(dpt))
		{
			error_ptr = "'dpt' is not a number!";
			goto error;
		}

		ga_t *_ga = calloc(1, sizeof(ga_t));

		if (prev_ga != NULL)
			prev_ga->next = _ga;
		else
			config.gas = _ga;

		address_t ga_addr = {.ga={line, area, member}};
		_ga->addr = ga_addr;
		_ga->dpt = (uint8_t)dpt->valueint;
		_ga->series = strdup(series->valuestring);
		prev_ga = _ga;

		cJSON *ignored_senders = cJSON_GetObjectItemCaseSensitive(ga_obj, "ignored_senders");
		if (!cJSON_IsArray(ignored_senders))
		{
			error_ptr = "'ignored_senders' is not an array!";
			goto error;
		}
		_ga->ignored_senders_len = cJSON_GetArraySize(ignored_senders);
		_ga->ignored_senders = calloc(_ga->ignored_senders_len, sizeof(address_t));
		int i = 0;
		cJSON *ignored_sender = NULL;
		cJSON_ArrayForEach(ignored_sender, ignored_senders)
		{
			if (!cJSON_IsString(ignored_sender))
			{
				error_ptr = "Expected array of strings, got something that is not a string in 'ignored_senders'";
				goto error;
			}
			if (ignored_sender->valuestring == NULL)
			{
				error_ptr = "Got empty string instead of a physical address in 'ignored_senders'";
				goto error;
			}
			uint32_t area, line, member;
			sscanf(ignored_sender->valuestring, "%u.%u.%u", &area, &line, &member);
			_ga->ignored_senders[i].pa.area = area;
			_ga->ignored_senders[i].pa.line = line;
			_ga->ignored_senders[i].pa.member = member;
			++i;
		}

		cJSON *tags = cJSON_GetObjectItemCaseSensitive(ga_obj, "tags");
		if (!cJSON_IsArray(tags))
		{
			error_ptr = "'tags' is not an array!";
			goto error;
		}
		_ga->tags_len = cJSON_GetArraySize(tags);
		_ga->tags = calloc(_ga->tags_len, sizeof(char *));
		i = 0;
		cJSON *tag = NULL;
		cJSON_ArrayForEach(tag, tags)
		{
			if (!cJSON_IsString(tag))
			{
				error_ptr = "Expected array of string, got something that is not a string in 'tags'";
				goto error;
			}
			if (tag->valuestring == NULL)
			{
				error_ptr = "Got empty string instead of a key=value pair in 'tags'";
				goto error;
			}
			_ga->tags[i] = strdup(tag->valuestring);
			++i;
		}
	}
	
	goto end;

error:
	printf("JSON error: %s\n", error_ptr);
	cJSON_Delete(json);
	status = -1;
end:
	free(json_str);
	return status;
}

int main(int argc, char *argv)
{
	// Parse config first
	
	if (parse_config() < 0)
	{
		exit(EXIT_FAILURE);
	}

	printf("Sending data to %s / %s\n", config.host, config.database);

	struct sockaddr_in sin = {};
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(MULTICAST_PORT);
	if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket: ");
		exit(EXIT_FAILURE);
	}
	printf("Our socket fd is %d\n", socket_fd);

	int loop = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (SO_REUSEADDR): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (bind(socket_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		perror("bind: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (IP_MULTICAST_LOOP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	command.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
	command.imr_interface.s_addr = inet_addr(INTERFACE_IP);

	if (command.imr_multiaddr.s_addr == -1)
	{
		perror(MULTICAST_IP" is not a valid multicast address: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_ADD_MEMBERSHIP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	atexit(exithandler);

	uint8_t buf[512];
	ssize_t rec = 0;

	while(1)
	{
		int sin_len = sizeof(sin);
		if ((rec = recvfrom(socket_fd, buf, 512, 0, (struct sockaddr *) &sin, &sin_len)) == -1)
		{
			perror("recfrom: ");
			break;
		}
		/*
		printf("Got %d bytes: ", rec);
		for (ssize_t i = 0; i < rec; ++i)
			printf("%02x ", buf[i]);
		printf("\n");
		*/
		process_packet(buf, rec);
	}

	return 0;
}

