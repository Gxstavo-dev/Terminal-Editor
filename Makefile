CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags ncursesw)
LDLIBS += $(shell $(PKG_CONFIG) --libs ncursesw)

SOURCES = src/main.c src/ui.c src/buffer.c src/files.c src/config.c src/lint.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(wildcard src/*.h)
TARGET = terminal-editor

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
