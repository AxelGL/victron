OS=$(shell uname -s)
#ifneq ("$(wildcard .git)","")
#CFLAGS?=-g -Wall
#VERSION := $(shell git describe --dirty --tags | sed 's/^v//')
#CURR_VERSION := $(shell sed 's/^\#define VERSION "\(.*\)"$$/\1/' version.h 2>/dev/null)
#else
#BASE_VERSION := $(shell cat base_version)
#endif
BINDIR=/usr/local/bin
ifeq ($(OS),Linux)
LDLIBS?=-pthread -lutil -lpigpio
BINDIR=/usr/bin
INSTGROUP=root
else
INSTGROUP=wheel
ifneq ($(OS),Darwin)
LDLIBS?=-lpthread -lutil
endif
endif

objects=victron.o

all: version victron

.PHONY: version
version:
	@if [ "$(VERSION)" != "$(CURR_VERSION)" ]; then \
	echo '#define VERSION "'$(VERSION)'"' > version.h; \
	fi

victron: $(objects)
	$(CC) -g -o victron $(objects) $(LDFLAGS) $(LDLIBS)


version.h:
	@echo '#define VERSION "'$(BASE_VERSION)'"' > version.h


clean:
	rm -f victron $(objects)
