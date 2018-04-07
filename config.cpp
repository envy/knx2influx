#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "cJSON.h"
#include "config.h"

void print_config(config_t *config)
{
	// Sender tags
	for (uint16_t i = 0; i < UINT16_MAX; ++i)
	{
		if (config->sender_tags[i] != NULL)
		{
			knxnet::address_t a = {.value = i};
			printf("%2u.%2u.%3u ", a.pa.area, a.pa.line, a.pa.member);
			bool first = true;
			tags_t *entry = config->sender_tags[i];
			while (entry != NULL)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					printf("          ");
				}
				printf("+ [");
				bool first_tag = true;
				for (size_t i = 0; i < entry->tags_len; ++i)
				{
					if (first_tag)
					{
						first_tag = false;
					}
					else
					{
						printf(", ");
					}
					printf("%s", entry->tags[i]);
				}
				printf("]\n");
				entry = entry->next;
			}
		}
	}
	printf("\n");

	// GA tags
	for (uint16_t i = 0; i < UINT16_MAX; ++i)
	{
		if (config->ga_tags[i] != NULL)
		{
			knxnet::address_t a = {.value = i};
			bool first = true;
			tags_t *entry = config->ga_tags[i];

			if (entry->read_on_startup)
			{
				printf(">");
			}
			else
			{
				printf(" ");
			}
			printf("%2u/%2u/%3u ", a.ga.area, a.ga.line, a.ga.member);

			while (entry != NULL)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					printf("           ");
				}
				printf("+ [");
				bool first_tag = true;
				for (size_t i = 0; i < entry->tags_len; ++i)
				{
					if (first_tag)
					{
						first_tag = false;
					}
					else
					{
						printf(", ");
					}
					printf("%s", entry->tags[i]);
				}
				printf("]\n");
				entry = entry->next;
			}
		}
	}
	printf("\n");

	// Group addresses
	for (uint16_t i = 0; i < UINT16_MAX; ++i)
	{
		if (config->gas[i] != NULL)
		{
			knxnet::address_t a = {.value = i};
			printf("%2u/%2u/%3u ", a.ga.area, a.ga.line, a.ga.member);
			ga_t *entry = config->gas[i];
			bool first = true;
			while (entry != NULL)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					printf("          ");
				}
				printf("-> %s (DPT %u%s) ", entry->series, entry->dpt, entry->convert_dpt1_to_int == 1 ? " conv to int" : "");

				bool first_tag = true;
				printf("[");
				for (size_t i = 0; i < entry->tags_len; ++i)
				{
					if (first_tag)
					{
						first_tag = false;
					}
					else
					{
						printf(", ");
					}
					printf("%s", entry->tags[i]);
				}
				printf("]\n");

				entry = entry->next;
			}
		}
	}

	exit(0);
}

long safe_strtol(char const *str, char **endptr, int base)
{
	errno = 0;
	long val = strtol(str, endptr, base);
	if (errno != 0)
	{
		printf("error parsing value in GA\n");
		exit(EXIT_FAILURE);
	}
	return val;
}

knxnet::address_t *parse_pa(const char *pa)
{
	// printf("parsing %s\n", pa);
	auto addrs = parse_addr(pa, ".", 15, 15);
	// address_t *cur = addrs;
	// while (cur->value != 0)
	// {
	// 	printf("%u.%u.%u\n", cur->pa.area, cur->pa.line, cur->pa.member);
	// 	cur++;
	// }

	// printf("----------\n");
	return addrs;
}

knxnet::address_t *parse_ga(const char *ga)
{
	// printf("parsing %s\n", ga);
	auto addrs = parse_addr(ga, "/", 31, 7);
	// address_t *cur = addrs;
	// while (cur->value != 0)
	// {
	// 	printf("%u/%u/%u\n", cur->ga.area, cur->ga.line, cur->ga.member);
	// 	cur++;
	// }

	// printf("----------\n");
	return addrs;
}

knxnet::address_t *parse_addr(const char *addr_s, const char *sep, uint8_t area_max, uint8_t line_max)
{
	char *string = strdup(addr_s);
	char *tofree = string;
	uint8_t astart = 0, aend = 0;
	uint8_t lstart = 0, lend = 0;
	uint8_t mstart = 0, mend = 0;
	uint16_t acount = 0, lcount = 0, mcount = 0;

	char *area_s = strsep(&string, sep);
	if (area_s == NULL)
	{
		printf("error parsing addr (area)\n");
		exit(EXIT_FAILURE);
	}
	char *line_s = strsep(&string, sep);
	if (line_s == NULL)
	{
		printf("error parsing addr (line)\n");
		exit(EXIT_FAILURE);
	}
	char *member_s = strsep(&string, sep);
	if (member_s == NULL)
	{
		printf("error parsing addr (member)\n");
		exit(EXIT_FAILURE);
	}

	// Area
	if (area_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&area_s, "-");
		if (start_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		start_s++;
		char *end_s = strsep(&area_s, "-");
		if (end_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		end_s[strlen(end_s)-1] = '\0';
		//printf("%s - %s\n", start_s, end_s);
		astart = safe_strtol(start_s, NULL, 10);
		aend = safe_strtol(end_s, NULL, 10);
		acount = aend - astart + 1;
	}
	else if (area_s[0] == '*')
	{
		// Wildard
		astart = 0;
		aend = area_max;
		acount = area_max + 1;
	}
	else
	{
		astart = safe_strtol(area_s, NULL, 10);
		aend = astart;
		acount = 1;
	}

	// Line
	if (line_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&line_s, "-");
		if (start_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		start_s++;
		char *end_s = strsep(&line_s, "-");
		if (end_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		end_s[strlen(end_s)-1] = '\0';
		//printf("%s - %s\n", start_s, end_s);
		lstart = safe_strtol(start_s, NULL, 10);
		lend = safe_strtol(end_s, NULL, 10);
		lcount = lend - lstart + 1;
	}
	else if (line_s[0] == '*')
	{
		// Wildard
		lstart = 0;
		lend = line_max;
		lcount = line_max + 1;
	}
	else
	{
		lstart = safe_strtol(line_s, NULL, 10);
		lend = lstart;
		lcount = 1;
	}

	// Member
	if (member_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&member_s, "-");
		if (start_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		start_s++;
		char *end_s = strsep(&member_s, "-");
		if (end_s == NULL)
		{
			printf("error parsing range addr (start)\n");
			exit(EXIT_FAILURE);
		}
		end_s[strlen(end_s)-1] = '\0';
		//printf("%s - %s\n", start_s, end_s);
		mstart = safe_strtol(start_s, NULL, 10);
		mend = safe_strtol(end_s, NULL, 10);
		mcount = mend - mstart + 1;
	}
	else if (member_s[0] == '*')
	{
		// Wildard
		mstart = 0;
		mend = 255;
		mcount = 256;
	}
	else
	{
		mstart = safe_strtol(member_s, NULL, 10);
		mend = mstart;
		mcount = 1;
	}

	free(tofree);
	// Allocate enough + 1 for end marker
	knxnet::address_t *addr = (knxnet::address_t *)calloc(acount * lcount * mcount + 1, sizeof(knxnet::address_t));
	//printf("alloc: %p\n", addr);
	knxnet::address_t *cur = addr;
	for (uint16_t a = astart; a <= aend; ++a)
		for (uint16_t l = lstart; l <= lend; ++l)
			for (uint16_t m = mstart; m <= mend; ++m)
			{
				if (a == 0 && l == 0 && m == 0)
				{
					continue;
				}

				// Hacky
				if (line_max == 7)
				{
					cur->ga.area = a;
					cur->ga.line = l;
					cur->ga.member = m;
				}
				else
				{
					cur->pa.area = a;
					cur->pa.line = l;
					cur->pa.member = m;
				}

				cur++;
			}

	return addr;
}

int parse_config(config_t *config)
{
	int status = 0;

	// Open config file
	FILE *f = fopen("knx2influx.json", "rb");
	if (f == NULL)
	{
		printf("Could not find config file knx2influx.json!\n");
		status = -1;
		return status;
	}
	fseek(f, 0, SEEK_END);
	uint64_t fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	// Read file
	char *json_str = (char *)malloc(fsize + 1);
	fread(json_str, fsize, 1, f);
	fclose(f);
	json_str[fsize] = '\0';

	std::string error_ptr;
	// And parse
	
	int i = 0;
	cJSON *json = cJSON_Parse(json_str);
	cJSON *obj1 = nullptr, *obj2 = nullptr, *obj3 = nullptr, *obj4 = nullptr;
	knxnet::address_t *cur = nullptr, *addrs1 = nullptr;

	if (json == NULL)
	{
		error_ptr = (char *)cJSON_GetErrorPtr();
		goto error;
	}

	obj1 = cJSON_GetObjectItemCaseSensitive(json, "interface");
	if (cJSON_IsString(obj1) && (obj1->valuestring != NULL))
	{
		config->interface = strdup(obj1->valuestring);
	}
	else
	{
		error_ptr = "No interface given in config!";
		goto error;
	}

	obj1 = cJSON_GetObjectItemCaseSensitive(json, "host");
	if (cJSON_IsString(obj1) && (obj1->valuestring != NULL))
	{
		config->host = strdup(obj1->valuestring);
	}
	else
	{
		error_ptr = "No host given in config!";
		goto error;
	}

	obj1 = cJSON_GetObjectItemCaseSensitive(json, "database");
	if (cJSON_IsString(obj1) && (obj1->valuestring != NULL))
	{
		config->database = strdup(obj1->valuestring);
	}
	else
	{
		error_ptr = "No database given in config!";
		goto error;
	}

	obj1 = cJSON_GetObjectItemCaseSensitive(json, "user");
	if (obj1 && cJSON_IsString(obj1) && (obj1->valuestring != NULL))
	{
		config->user = strdup(obj1->valuestring);
	}

	obj1 = cJSON_GetObjectItemCaseSensitive(json, "password");
	if (obj1 && cJSON_IsString(obj1) && (obj1->valuestring != NULL))
	{
		config->password = strdup(obj1->valuestring);
	}

	// Sender tags
	obj1 = cJSON_GetObjectItemCaseSensitive(json, "sender_tags");
	// sender_tags is optional
	if (obj1)
	{
		if (!cJSON_IsObject(obj1))
		{
			error_ptr = "Expected object, got something else for 'sender_tags'";
			goto error;
		}

		obj2 = NULL;
		cJSON_ArrayForEach(obj2, obj1)
		{
			tags_t sender_tag = {};

			if (!cJSON_IsArray(obj2))
			{
				error_ptr = "Expected array as value, got something else for entry in 'sender_tags'";
				goto error;
			}

			sender_tag.tags_len = cJSON_GetArraySize(obj2);
			//sender_tag.tags = (char **)calloc(sender_tag.tags_len, sizeof(char *));
			sender_tag.tags = new char*[sender_tag.tags_len];
			
			i = 0;

			obj3 = NULL;
			cJSON_ArrayForEach(obj3, obj2)
			{
				if (!cJSON_IsString(obj3))
				{
					error_ptr = "Expected string for tag entry in 'sender_tags'";
					goto error;
				}
				if (obj3->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a tag entry in 'sender_tags'";
					goto error;
				}

				sender_tag.tags[i] = strdup(obj3->valuestring);

				++i;
			}

			addrs1 = parse_pa(obj2->string);
			cur = addrs1;
			while (cur->value != 0)
			{
				tags_t *__sender_tag;
				//__sender_tag = (tags_t *)calloc(1, sizeof(tags_t));
				__sender_tag = new tags_t;
				memcpy(__sender_tag, &sender_tag, sizeof(tags_t));

				if (config->sender_tags[cur->value] == NULL)
				{
					config->sender_tags[cur->value] = __sender_tag;
				}
				else
				{
					tags_t *entry;
					entry = config->sender_tags[cur->value];
					while (entry->next != NULL)
						entry = entry->next;
					entry->next = __sender_tag;
				}
				cur++;
			}
			//printf("free: %p\n", addrs);
			free(addrs1);
		}
	}

	// Group address tags
	obj1 = cJSON_GetObjectItemCaseSensitive(json, "ga_tags");
	// ga_tags is optional
	if (obj1)
	{
		if (!cJSON_IsObject(obj1))
		{
			error_ptr = "Expected object, got something else for 'ga_tags'";
			goto error;
		}

		obj2 = NULL;
		cJSON_ArrayForEach(obj2, obj1)
		{
			tags_t ga_tag = {};

			if (!cJSON_IsArray(obj2))
			{
				error_ptr = "Expected array as value, got something else for entry in 'sender_tags'";
				goto error;
			}

			ga_tag.tags_len = cJSON_GetArraySize(obj2);
			//ga_tag.tags = (char **)calloc(_ga_tag.tags_len, sizeof(char *));
			ga_tag.tags = new char*[ga_tag.tags_len];

			i = 0;

			obj3 = NULL;
			cJSON_ArrayForEach(obj3, obj2)
			{
				if (!cJSON_IsString(obj3))
				{
					error_ptr = "Expected string for tag entry in 'sender_tags'";
					goto error;
				}
				if (obj3->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a tag entry in 'sender_tags'";
					goto error;
				}

				ga_tag.tags[i] = strdup(obj3->valuestring);

				++i;
			}

			addrs1 = parse_ga(obj2->string);
			cur = addrs1;

			while (cur->value != 0)
			{
				tags_t *__ga_tag;
				//__ga_tag = (tags_t *)calloc(1, sizeof(tags_t));
				__ga_tag = new tags_t;
				memcpy(__ga_tag, &ga_tag, sizeof(tags_t));

				if (config->ga_tags[cur->value] == NULL)
				{
					config->ga_tags[cur->value] = __ga_tag;
				}
				else
				{
					tags_t *entry;
					entry = config->ga_tags[cur->value];
					while (entry->next != NULL)
						entry = entry->next;
					entry->next = __ga_tag;
				}
				cur++;
			}
			//printf("free: %p\n", addrs);
			free(addrs1);
		}
	}

	// Startup reads
	obj1 = cJSON_GetObjectItemCaseSensitive(json, "read_on_startup");
	// read_on_startup is optional
	if (obj1)
	{
		if (!cJSON_IsArray(obj1))
		{
			error_ptr = "Expected array, got something else for 'ga_tags'";
			goto error;
		}

		obj2 = NULL;
		cJSON_ArrayForEach(obj2, obj1)
		{
			if (!cJSON_IsString(obj2))
			{
				error_ptr = "Expected string for GA entry in 'read_on_startup'";
				goto error;
			}
			if (obj2->valuestring == NULL)
			{
				error_ptr = "Got empty string instead of a GA entry in 'read_on_startup'";
				goto error;
			}

			addrs1 = parse_ga(obj2->valuestring);
			cur = addrs1;

			while (cur->value != 0)
			{
				if (config->ga_tags[cur->value] == NULL)
				{
					//config->ga_tags[cur->value] = (tags_t *)calloc(1, sizeof(tags_t));
					config->ga_tags[cur->value] = new tags_t;
					config->ga_tags[cur->value]->read_on_startup = true;
				}
				else
				{
					tags_t *entry;
					entry = config->ga_tags[cur->value];
					while (entry != NULL)
					{
						entry->read_on_startup = true;
						entry = entry->next;
					}
				}
				cur++;
			}
			free(addrs1);
		}
	}

	// Group addresses
	obj1 = cJSON_GetObjectItemCaseSensitive(json, "gas");
	if (!cJSON_IsArray(obj1))
	{
		error_ptr = "Expected array, got something else for 'gas'";
		goto error;
	}
	
	obj2 = NULL;

	cJSON_ArrayForEach(obj2, obj1)
	{
		if (!cJSON_IsObject(obj2))
		{
			error_ptr = "Expected array of ojects, got something that is not object in 'gas'";
			goto error;
		}	

		ga_t _ga = {};

		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "series");
		if (!cJSON_IsString(obj3))
		{
			error_ptr = "'series' is not a string!";
			goto error;
		}
		if (obj3->valuestring == NULL)
		{
			error_ptr = "'series' must not be empty!";
			goto error;
		}
		//printf("Series: %s\n", series->valuestring);
		_ga.series = strdup(obj3->valuestring);

		// Read out DPT
		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "dpt");
		if (!cJSON_IsNumber(obj3))
		{
			error_ptr = "'dpt' is not a number!";
			goto error;
		}
		_ga.dpt = (uint8_t)obj3->valueint;

		// If DPT is 1, find out if we should convert to int
		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "convert_to_int");
		i = 0;
		if (obj3)
		{
			if (!cJSON_IsBool(obj3))
			{
				error_ptr = "'convert_to_int' is not a bool!";
				goto error;
			}

			i = obj3->type == cJSON_True ? 1 : 0;
		}
		_ga.convert_dpt1_to_int = i;

		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "ignored_senders");
		if (obj3)
		{
			if (!cJSON_IsArray(obj3))
			{
				error_ptr = "'ignored_senders' is not an array!";
				goto error;
			}
			_ga.ignored_senders_len = cJSON_GetArraySize(obj3);
			//_ga.ignored_senders = (knxnet::address_t *)calloc(_ga.ignored_senders_len, sizeof(knxnet::address_t));
			_ga.ignored_senders = new knxnet::address_t[_ga.ignored_senders_len];

			i = 0;
			obj4 = NULL;
			cJSON_ArrayForEach(obj4, obj3)
			{
				if (!cJSON_IsString(obj4))
				{
					error_ptr = "Expected array of strings, got something that is not a string in 'ignored_senders'";
					goto error;
				}
				if (obj4->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a physical address in 'ignored_senders'";
					goto error;
				}
				uint32_t area, line, member;
				sscanf(obj4->valuestring, "%u.%u.%u", &area, &line, &member);
				_ga.ignored_senders[i].pa.area = area;
				_ga.ignored_senders[i].pa.line = line;
				_ga.ignored_senders[i].pa.member = member;
				++i;
			}
		}

		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "tags");
		if (obj3)
		{
			if (!cJSON_IsArray(obj3))
			{
				error_ptr = "'tags' is not an array!";
				goto error;
			}
			_ga.tags_len = cJSON_GetArraySize(obj3);
			//_ga.tags = (char **)calloc(_ga.tags_len, sizeof(char *));
			_ga.tags = new char*[_ga.tags_len];
			
			i = 0;
			obj4 = NULL;
			cJSON_ArrayForEach(obj4, obj3)
			{
				if (!cJSON_IsString(obj4))
				{
					error_ptr = "Expected array of string, got something that is not a string in 'tags'";
					goto error;
				}
				if (obj4->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a key=value pair in 'tags'";
					goto error;
				}
				_ga.tags[i] = strdup(obj4->valuestring);
				++i;
			}
		}
		else
		{
			_ga.tags_len = 0;
			_ga.tags = NULL;
		}

		obj3 = cJSON_GetObjectItemCaseSensitive(obj2, "ga");
		if (!cJSON_IsString(obj3))
		{
			error_ptr = "'ga' is not a string!";
			goto error;
		}
		if (obj3->valuestring == NULL)
		{
			error_ptr = "'ga' must not be empty!";
			goto error;
		}
		//printf("GA: %s", ga->valuestring);

		addrs1 = parse_ga(obj3->valuestring);
		cur = addrs1;

		while (cur->value != 0)
		{
			ga_t *entry;
			entry = config->gas[cur->value];

			ga_t *__ga;
			//__ga = (ga_t *)calloc(1, sizeof(ga_t));
			__ga = new ga_t;
			memcpy(__ga, &_ga, sizeof(ga_t));

			if (entry == NULL)
			{
				config->gas[cur->value] = __ga;
			}
			else
			{
				while (entry->next != NULL)
				{
					entry = entry->next;
				}

				entry->next = __ga;
			}

			cur++;
		}
		//printf("free: %p\n", addrs);
		free(addrs1);
	}

	goto end;

error:
	std::cerr << "JSON error: " <<  error_ptr << std::endl;
	status = -1;
end:
	cJSON_Delete(json);
	free(json_str);
	return status;
}
