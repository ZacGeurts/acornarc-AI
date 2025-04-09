# Makefile
CC = g++
CFLAGS = -Wall -O2 -fPIC -std=c++17 -I include  # Updated to C++17
LDFLAGS = -shared -lz                           # Link with zlib
TARGET = acornarc_core.so
SOURCES = src/core.cpp src/cpu.cpp src/memory.cpp src/io.cpp  # Added io.cpp
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

# Include dependency files
-include $(DEPS)

# Compile and generate dependency files
%.o: %.cpp
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Clean up
clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

# Phony targets
.PHONY: all clean