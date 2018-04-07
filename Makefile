COMMONFLAGS += -O0 -g -I. -I./libknxnet -Wall
CXXFLAGS += $(COMMONFLAGS) -std=c++11
CFLAGS += $(COMMONFLAGS) -std=c11
LDFLAGS += -lcurl -lm -lpthread

HEADERS := knx.h config.h

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

knx2influx: knx2influx.o conversion.o cJSON.o config.o libknxnet/libknxnet.a
	$(CXX) $(LDFLAGS) $^ -o $@

clean:
	rm -rf *.o knx2influx
