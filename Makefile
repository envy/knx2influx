CFLAGS += -std=c11 -O0 -g
LDFLAGS += -lcurl -lm

knx2influx: knx2influx.o cJSON.o
