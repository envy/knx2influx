#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"

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
	char *json_str = malloc(fsize + 1);
	fread(json_str, fsize, 1, f);
	fclose(f);
	json_str[fsize] = '\0';

	char *error_ptr;
	// And parse
	cJSON *json = cJSON_Parse(json_str);

	if (json == NULL)
	{
		error_ptr = (char *)cJSON_GetErrorPtr();
		goto end;
	}

	cJSON *host = cJSON_GetObjectItemCaseSensitive(json, "host");
	if (cJSON_IsString(host) && (host->valuestring != NULL))
	{
		config->host = strdup(host->valuestring);
	}
	else
	{
		error_ptr = "No host given in config!";
		goto error;
	}

	cJSON *database = cJSON_GetObjectItemCaseSensitive(json, "database");
	if (cJSON_IsString(database) && (database->valuestring != NULL))
	{
		config->database = strdup(database->valuestring);
	}
	else
	{
		error_ptr = "No database given in config!";
		goto error;
	}
	cJSON *user = cJSON_GetObjectItemCaseSensitive(json, "user");
	if (user && cJSON_IsString(user) && (user->valuestring != NULL))
	{
		config->database = strdup(user->valuestring);
	}
	cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
	if (password && cJSON_IsString(password) && (password->valuestring != NULL))
	{
		config->password = strdup(password->valuestring);
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
		//printf("GA: %s", ga->valuestring);

		uint32_t area, line, member;

		sscanf(ga->valuestring, "%u/%u/%u", &area, &line, &member);

		//printf(" -> %u/%u/%u\n", area, line, member);

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
		//printf("Series: %s\n", series->valuestring);

		// Read out DPT
		cJSON *dpt = cJSON_GetObjectItemCaseSensitive(ga_obj, "dpt");
		if (!cJSON_IsNumber(dpt))
		{
			error_ptr = "'dpt' is not a number!";
			goto error;
		}

		// If DPT is 1, find out if we should convert to int
		cJSON *convert_to_int = cJSON_GetObjectItemCaseSensitive(ga_obj, "convert_to_int");
		uint8_t convert_dpt1_to_int = 0;
		if (convert_to_int != NULL)
		{
			if (!cJSON_IsBool(convert_to_int))
			{
				error_ptr = "'convert_to_int' is not a bool!";
				goto error;
			}

			convert_dpt1_to_int = convert_to_int->type == cJSON_True ? 1 : 0;
		}



		address_t ga_addr = {.ga={line, area, member}};
		ga_t *_ga = calloc(1, sizeof(ga_t));

		ga_t *entry = config->gas[ga_addr.value];

		if (entry == NULL)
		{
			config->gas[ga_addr.value] = _ga;
		}
		else
		{
			while (entry->next != NULL)
			{
				entry = entry->next;
			}

			entry->next = _ga;
		}

		_ga->addr = ga_addr;
		_ga->dpt = (uint8_t)dpt->valueint;
		_ga->convert_dpt1_to_int = convert_dpt1_to_int;
		_ga->series = strdup(series->valuestring);
		prev_ga = _ga;

		cJSON *ignored_senders = cJSON_GetObjectItemCaseSensitive(ga_obj, "ignored_senders");
		if (ignored_senders)
		{
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
		}

		cJSON *tags = cJSON_GetObjectItemCaseSensitive(ga_obj, "tags");
		if (tags)
		{
			if (!cJSON_IsArray(tags))
			{
				error_ptr = "'tags' is not an array!";
				goto error;
			}
			_ga->tags_len = cJSON_GetArraySize(tags);
			_ga->tags = calloc(_ga->tags_len, sizeof(char *));
			int i = 0;
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
	}

	goto end;

error:
	printf("JSON error: %s\n", error_ptr);
	status = -1;
end:
	cJSON_Delete(json);
	free(json_str);
	return status;
}
