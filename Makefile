CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
PREFIX ?= $(HOME)/.local

.PHONY: all install check clean
all: build/check-tsc

build/check-tsc: tools/check-tsc.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< -lm

install:
	install -Dm755 bin/dota2-tsc-fix "$(DESTDIR)$(PREFIX)/bin/dota2-tsc-fix"

check: build/check-tsc
	bash -n bin/dota2-tsc-fix
	./bin/dota2-tsc-fix --help >/dev/null

clean:
	rm -f build/check-tsc
