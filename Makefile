CFLAGS = -Wall -std=gnu99 -D_POSIX_C_SOURCE=200809L $(shell pkg-config --cflags cairo xcb xcb-util)
LDFLAGS = -lm $(shell pkg-config --libs cairo xcb xcb-util)

-include config.mk

ifdef DEBUG
	CFLAGS += -ggdb3 -DDEBUG -Werror=implicit-function-declaration
else
	CFLAGS += -O2 -DNDEBUG
endif

all: build/monsterbar

build:
	echo $(CFLAGS)
	@mkdir -p build

build/%.o: src/%.c | build
	@gcc -c $(CFLAGS) $< -o $@
	@echo "  CC    " $<

build/monsterbar: $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
	@gcc $(CFLAGS) $(LDFLAGS) $^ -o $@
	@echo "  LD    " $@
