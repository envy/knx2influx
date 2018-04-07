COMMONFLAGS += -O0 -g -I. -I./libknxnet
CXXFLAGS += $(COMMONFLAGS) -std=c++11
LDFLAGS += -lcurl -lm -lpthread

knx2influx: knx2influx.o conversion.o cJSON.o config.o libknxnet/libknxnet.a
	$(CXX) $(LDFLAGS) $^ -o $@

clean:
	rm -rf *.o knx2influx
