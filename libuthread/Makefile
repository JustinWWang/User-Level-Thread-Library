# Compile these
TARGETS := \
	sem.o \
	tps.o

# Neccessary for library
OBJ := \
	sem.o \
	tps.o \
	queue.o \
	thread.o

lib := libuthread.a

DEPS := $(patsubst %.o,%.h,$(OBJ))

CUR_PWD := $(shell pwd)

ARCH=ar
CC=gcc

ARCH_FLAGS=rcs
CFLAGS=-I -std=c99 -Wall -Wextra -ftrapv -D_FORTIFY_SOURCE=2 -Wfloat-equal -Wpointer-arith -O0 -g2

# Don't print the commands unless explicitly requested with `make V=1`
ifneq ($(V),1)
Q = @
V = 0
endif

all: $(lib)

%.o: %.c $(DEPS)
	@echo "compile	$@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(lib): $(OBJ)
	@echo "LD 	$@"
	$(Q)$(ARCH) $(ARCH_FLAGS) $(lib) $^

# Leave queue.o and thread.o alone
clean:
	@echo "CLEAN	$(CUR_PWD)"
	$(Q)rm -f $(TARGETS) $(lib)
