CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g -I../include
SRC      = ../dual_clamp.c ../pool_scheduler.c ../pool_failover.c
TEST     = test_dual_pool.c

run: build
	./test_dual_pool

build: $(SRC) $(TEST)
	$(CC) $(CFLAGS) -o test_dual_pool $(TEST) $(SRC)

clean:
	rm -f test_dual_pool
