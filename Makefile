CC=gcc
CFLAGS=-Wall -Werror -g

.PHONY: all
all: anna

OBJS := $(patsubst %.c,%.o,$(wildcard *.c))

TARGET=anna

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) sqlite3.lib

.PHONY: clean
clean:
	-rm -f *.o *.a *~ core $(TARGET) $(DEPDIR)/*.d

DEPDIR = .deps
DEPFILE = $(DEPDIR)/$(subst /,_,$*.d)
%.o : %.c
	-@[ -d $(DEPDIR) ] || mkdir -p $(DEPDIR)
	$(CC) $(CFLAGS) -c $< -o $(@:.gcno=.o) -MD -MP -MF $(DEPFILE)

-include $(DEPDIR)/*.d
