CFLAGS = -Wall -std=gnu99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500 $(shell pkg-config --cflags cairo json-c xcb xcb-util)
LDFLAGS = -lm $(shell pkg-config --libs cairo json-c xcb xcb-util)

-include config.mk

ifdef DEBUG
	CFLAGS += -ggdb3 -DDEBUG -Werror=implicit-function-declaration
else
	CFLAGS += -O2 -DNDEBUG
endif

all: build/monsterbar build/i3glow

build:
	echo $(CFLAGS)
	@mkdir -p build

build/%.o: src/%.c | build
	@gcc -c $(CFLAGS) $< -o $@
	@echo "  CC    " $<

build/i3glow: build/i3glow.o build/util.o
	@gcc $(CFLAGS) $(LDFLAGS) $^ -o $@
	@echo "  LD    " $@

build/monsterbar: build/monsterbar.o build/util.o
	@gcc $(CFLAGS) $(LDFLAGS) $^ -o $@
	@echo "  LD    " $@
