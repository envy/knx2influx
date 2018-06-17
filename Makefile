COMMONFLAGS += -O0 -g -I. -I./libknxnet -Wall -fpie -fstack-protector-strong
CXXFLAGS += $(COMMONFLAGS) -std=c++11
CFLAGS += $(COMMONFLAGS) -std=c11
LDFLAGS += -lcurl -lm -lpthread -pie

HEADERS := knx.h config.h

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

knx2influx: knx2influx.o cJSON.o config.o libknxnet/libknxnet.a
	$(CXX) $(LDFLAGS) $^ -o $@

libknxnet/libknxnet.a:
	cd libknxnet && make

clean:
	rm -rf *.o knx2influx
