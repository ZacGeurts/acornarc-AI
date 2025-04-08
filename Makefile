# Makefile
CC = g++
CFLAGS = -Wall -O2 -fPIC -std=c++11  # Removed -shared from CFLAGS since it’s a linker flag
LDFLAGS = -shared -lz                 # Added -lz for zlib
TARGET = acornarc_core.so
SOURCES = src/core.cpp src/cpu.cpp src/memory.cpp
OBJECTS = $(SOURCES:.cpp=.o)
INCLUDE = -I include

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean