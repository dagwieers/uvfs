CC=/usr/bin/gcc
CXX=/usr/bin/gcc
CXXFLAGS = -g -O3 -Wall $(INCLUDES) $(DEFINES) -I.

LIBS = -lpthread -lc -lstdc++ -lm

# for MP machines add -D__SMP__ and CONFIG_SMP to CFLAGS line
CFLAGS += -I/usr/src/linux/include -I. -D__KERNEL__ -DMODULE -DUVFS_FS_NAME='"pmfs"' -DUVFS_IMPL_DELETE_INODE -DUVFS_IMPL_SYMLINK -DUVFS_IMPL_MKNOD -DUVFS_IMPL_LINK -DNSLOTS=4 -O -Wall 

CFILES = driver.c dentry.c operations.c file.c dir.c symlink.c init.c super.c

OBJS = $(subst .c,.o,$(CFILES))

exes : pmfs.o uvfs_signal uvfs_mount

libs : 
	echo No libs.

depend : 
	$(CXX) -M $(CFLAGS) $(CFILES) >.depend
	
pmfs.o : $(OBJS)
	$(LD) -r $^ -o $@

uvfs_signal : uvfs_signal.o
	$(CXX) $(CXXFLAGS) -o uvfs_signal uvfs_signal.o $(LIBS)

uvfs_mount : uvfs_mount.o 
	$(CXX) $(CXXFLAGS) -o uvfs_mount uvfs_mount.o $(LIBS)

clean : 
	rm -f $(OBJS) pmfs.o uvfs_mount.o uvfs_signal.o uvfs_mount uvfs_signal *~ #*


ifeq (.depend,$(wildcard .depend))
include .depend
endif

