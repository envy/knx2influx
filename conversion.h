#ifndef CONVERSION_H_
#define CONVERSION_H_

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

bool          data_to_bool(uint8_t *data);
int8_t        data_to_1byte_int(uint8_t *data);
uint8_t       data_to_1byte_uint(uint8_t *data);
int16_t       data_to_2byte_int(uint8_t *data);
uint16_t      data_to_2byte_uint(uint8_t *data);
float         data_to_2byte_float(uint8_t *data);
//color_t       data_to_3byte_color(uint8_t *data);
//time_of_day_t data_to_3byte_time(uint8_t *data);
//date_t        data_to_3byte_data(uint8_t *data);
int32_t       data_to_4byte_int(uint8_t *data);
uint32_t      data_to_4byte_uint(uint8_t *data);
float         data_to_4byte_float(uint8_t *data);

#endif
