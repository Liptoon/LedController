CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra $(shell pkg-config --cflags dbus-1 bluez)
LDFLAGS = -lsimpleble $(shell pkg-config --libs dbus-1 bluez) -lz
TARGET = led_controller
SRCS = main.cpp led_sniffer.cpp image_loader.cpp
HEADERS = led_sniffer.h image_loader.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean