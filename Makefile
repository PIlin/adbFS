all:


ADB_CFLAGS=-DADB_HOST=1 -I$(shell pwd) -fPIC $(shell pkg-config zlib --cflags)
ADB_LDFLAGS=-lpthread -lrt -fPIC $(shell pkg-config zlib --libs)

ADB_SOURCES :=
ADB_SOURCES +=adb_interface.c
ADB_SOURCES +=adb/adb_client.c
ADB_SOURCES +=adb/file_sync_client.c
# ADB_SOURCES +=adb/transport.c
ADB_SOURCES +=cutils/socket_loopback_client.c
ADB_SOURCES +=zipfile/centraldir.c
ADB_SOURCES +=zipfile/zipfile.c

ADB_OBJS := $(patsubst %.c,%.o,$(notdir $(ADB_SOURCES)))


CXXFLAGS=$(shell pkg-config fuse --cflags) $(ADB_CFLAGS)
LDFLAGS=$(shell pkg-config fuse --libs) $(ADB_LDFLAGS)

TARGET=adbfs




adb_interface.o: adb_interface.c Makefile
	$(CC) -c -o $@ $< $(ADB_CFLAGS)
%.o: adb/%.c Makefile
	$(CC) -c -o $@ $< $(ADB_CFLAGS)
%.o: cutils/%.c Makefile
	$(CC) -c -o $@ $< $(ADB_CFLAGS)
%.o: zipfile/%.c Makefile
	$(CC) -c -o $@ $< $(ADB_CFLAGS)


adb.so: $(ADB_OBJS)
	$(CC) -shared -o $@ $^ $(ADB_LDFLAGS)


all: $(TARGET)

adbfs.o: adbfs.cpp utils.h
	$(CXX) -c -o adbfs.o adbfs.cpp $(CXXFLAGS)

$(TARGET): adbfs.o $(ADB_OBJS)
	$(CXX) -o $(TARGET) $^ $(LDFLAGS)

.PHONY: clean

clean:
	rm -rf *.o html/ latex/ $(TARGET)

doc:
	doxygen Doxyfile
