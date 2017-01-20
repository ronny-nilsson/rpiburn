

OBJECTS := main.o high-load.o misc.o
OBJECTS += vchiq.o high-load-arm.o

name := rpiburn


CFLAGS += $(CROSS_CFLAGS) -O2 -g -Wall -std=gnu99
CFLAGS += -D_GNU_SOURCE -D_BSD_SOURCE -D_REENTRANT
CFLAGS += -fno-reorder-blocks -fno-reorder-blocks-and-partition
CFLAGS += -fno-toplevel-reorder -fno-crossjumping -falign-functions

AFLAGS += $(CFLAGS)


#-----------------------------													# Standard targets
.PHONY: all install
all: $(name)
install: $(prefix)/usr/sbin/$(name)


$(prefix)/usr/sbin/$(name): $(name)
	install -m 0755 -d "$(dir $@)"
	install -m 0755 $(name) "$@"
	touch "$@"


$(name): $(OBJECTS)
	$(CC) $(strip $(CFLAGS)) -o $@ $(OBJECTS) -lrt


%.o: %.c Makefile
	$(CC) $(strip $(CFLAGS)) -o $@ -c $<

%.o: %.S Makefile
	$(CC) $(strip $(AFLAGS)) -o $@ -c $<


#----------------------------													# Cleaning	
.PHONY: clean		
clean:
	rm -rf $(name) $(prefix)/usr/sbin/$(name) $(OBJECTS)

.PHONY: distclean
distclean: clean

