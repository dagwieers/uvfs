ifneq ($(UVFS_DEBUG),)
DPRINTK=-DDEBUG_PRINT
endif

CC=/usr/bin/gcc
CXX=/usr/bin/gcc
CXXFLAGS = -g -O3 -Wall $(INCLUDES) $(DEFINES) -I.

LIBS = -lpthread -lc -lstdc++ -lm

CFLAGS += $(DPRINTK) -D__KERNEL__ -DMODULE -DUVFS_FS_NAME='"pmfs"' -O -Wall

KERNELPATH := /lib/modules/$(shell uname -r)/build

obj-m := pmfs.o
pmfs-objs := dir.o driver.o file.o operations.o super.o symlink.o

all: uvfs_signal
	$(MAKE) -C $(KERNELPATH) SUBDIRS=$(CURDIR) modules

clean :
	rm -f $(pmfs-objs) pmfs.mod.c pmfs.mod.o pmfs.o pmfs.ko uvfs_signal.o uvfs_signal

uvfs_signal : uvfs_signal.o
	$(CXX) $(CXXFLAGS) -o uvfs_signal uvfs_signal.o $(LIBS)

ifeq (.depend,$(wildcard .depend))
include .depend
endif

macro-%:
	@echo '$($(*))'

env-%:
	@echo "$$$(*)"

