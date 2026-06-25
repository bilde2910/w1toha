CC=gcc
RM=rm -f
RMDIR=rm -rf
CFLAGS=-Werror -Wall -Wextra -Wpedantic -O3 -march=native -mtune=native
LDFLAGS=

SRCS=$(wildcard *.c)
OBJS=$(subst .c,.o,$(SRCS))
BINOUT=w1toha

# Main compilation target
all: $(BINOUT)

$(BINOUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BINOUT) $(OBJS)

# Compiled project dependencies
depend: .depend

.depend: $(SRCS)
	$(RM) ./.depend
	$(CC) $(CFLAGS) -MM $^>>./.depend;

# Remove all object files
clean:
	$(RM) $(OBJS)

# Remove all object and temporary files
distclean: clean
	$(RM) *~ .depend $(BINOUT)

# Remove everything
cleanall: distclean

# Run binary, create required data directory if missing
run:
	./$(BINOUT)

# Dynamically generated dependency file
include .depend
