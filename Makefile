CFLAGS += -std=c11 -O0 -g
LDFLAGS += -lcurl -lm -lpthread

knx2influx: knx2influx.o conversion.o cJSON.o config.o
