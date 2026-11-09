# SPDX-License-Identifier: MIT
# Makefile for QeafdropOs installer app

APP := installer
SOURCES := main.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := $(APP)

CFLAGS := -Wall -O2 -D_GNU_SOURCE
INCLUDES := -I../..

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
