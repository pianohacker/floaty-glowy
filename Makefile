CFLAGS = -Wall -std=gnu99
LDFLAGS = -lm -lxcb -lcairo

-include config.mk

ifdef DEBUG
	CFLAGS += -ggdb3 -DDEBUG -Werror=implicit-function-declaration
else
	CFLAGS += -O2 -DNDEBUG
endif

all: build/monsterbar

build:
	@mkdir -p build

build/%.o: src/%.c | build
	@gcc -c $(CFLAGS) $< -o $@
	@echo "  CC    " $<

build/monsterbar: $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
	@gcc $(CFLAGS) $(LDFLAGS) $^ -o $@
	@echo "  LD    " $@
