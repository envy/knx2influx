#include "conversion.h"

/**
 * Conversion functions
 */

bool data_to_bool(uint8_t *data)
{
	return (data[0] & 0x01) == 1 ? true : false;
}

int8_t data_to_1byte_int(uint8_t *data)
{
	return (int8_t)data[1];
}

uint8_t data_to_1byte_uint(uint8_t *data)
{
	return data[1];
}

int16_t data_to_2byte_int(uint8_t *data)
{
	return (int16_t)((data[1] << 8) | data[2]);
}

uint16_t data_to_2byte_uint(uint8_t *data)
{
	return (uint16_t)((data[1] << 8) | data[2]);
}

float data_to_2byte_float(uint8_t *data)
{
	//uint8_t  sign = (data[1] & 0b10000000) >> 7;
	uint8_t  expo = (data[1] & 0b01111000) >> 3;
	int16_t mant = ((data[1] & 0b10000111) << 8) | data[2];
	return 0.01f * mant * pow(2, expo);
}

/*
time_of_day_t data_to_3byte_time(uint8_t *data)
{
	time_of_day_t time;
	time.weekday = (weekday_t)((data[1] & 0b11100000) >> 5);
	time.hours = (data[1] & 0b00011111);
	time.minutes = (data[2] & 0b00111111);
	time.seconds = (data[3] & 0b00111111);
	return time;
}

date_t data_to_3byte_data(uint8_t *data)
{
	date_t date;
	date.day = (data[1] & 0b00011111);
	date.month = (data[2] & 0b00001111);
	date.year = (data[3] & 0b01111111);
	return date;
}

color_t data_to_3byte_color(uint8_t *data)
{
	color_t color;
	color.red = data[1];
	color.green = data[2];
	color.blue = data[3];
	return color;
}
*/

int32_t data_to_4byte_int(uint8_t *data)
{
	return (int32_t)((data[1] << 24) | (data[2] << 16) | (data[3] << 8) | (data[4] << 0));
}

uint32_t data_to_4byte_uint(uint8_t *data)
{
	return (uint32_t)((data[1] << 24) | (data[2] << 16) | (data[3] << 8) | (data[4] << 0));
}

float data_to_4byte_float(uint8_t *data)
{
	return (float)((data[1] << 24) | (data[2] << 16) | (data[3] << 8) |data[4]);
}
