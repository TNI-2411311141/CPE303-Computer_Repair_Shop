
CC ?= cc
CFLAGS += -Wall -Wextra
CFLAGS += -O2 -march=native
CFLAGS += -fstack-protector-strong
CFLAGS += -g3 -fsanitize=address,undefined

fastcgi_sqlite3: fastcgi_sqlite3.c
	$(CC) $(CFLAGS) -o $@ $< -lfcgi -lsqlite3

lint: fastcgi_sqlite3.c
	clang-tidy -checks=clang-analyzer-*,bugprone-*,performance-*,portability-*,readability-*,-readability-magic-numbers,-readability-identifier-length,-readability-braces-around-statements $<
