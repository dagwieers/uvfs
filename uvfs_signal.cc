// Signal the filesystem to shutdown spew debugging information.
// down the filesystem.

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "protocol.h"

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s devname command\n", argv[0]);
        fprintf(stderr, "where command can be 'shutdown' or 'status'\n");
        return 1;
    }
    int cmd;
    if (strcmp(argv[2], "shutdown") == 0)
    {
        cmd = UVFS_IOCTL_SHUTDOWN;
    }
    else if (strcmp(argv[2], "status") == 0)
    {
        cmd = UVFS_IOCTL_STATUS;
    }
    else if (strcmp(argv[2], "count") == 0)
    {
        cmd = UVFS_IOCTL_USE_COUNT;
    }
    else
    {
        fprintf(stderr, "Unrecognized command %s\n", argv[2]);
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        perror("Couldn't open device");
        return 1;
    }

    // execute requested command
    int result = ioctl(fd, cmd);
    if (result < 0)
    {
        fprintf(stderr,"%s ", argv[0]);
        perror("ioctl failed");
        close(fd);
        return 1;
    }

    if (cmd == UVFS_IOCTL_USE_COUNT)
    {
        fprintf(stdout, "opens on this module %d\n", result);
        fflush(stdout);
    }
    close(fd);
    return 0;
}
