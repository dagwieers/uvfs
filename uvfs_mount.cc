// Mount a uvfs filesystem.

#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include "protocol.h"
#include <signal.h>


// As far as I know this will work just as well as and 
// compatibly with the method used in mount.

static bool Locked = false;

static bool lock_mtab()
{
    for (int i = 0; i < 5; i++)
    {
        if (symlink("Garbage", "/etc/mtab~") == 0)
        {
            Locked = true;
            return true;
        }
        sleep(1);
    }
    return false;
}


static void unlock_mtab()
{
    unlink("/etc/mtab~");
    Locked = false;
}


void signalHandleAll(void (*f)(int))
{   
    struct sigaction newAction;
    struct sigaction oldAction;
    newAction.sa_handler = f;
    sigfillset(&newAction.sa_mask);
    newAction.sa_flags = 0;
    for (int i = 0; i < _NSIG; i++)
    {   
        sigaction(i, &newAction, &oldAction);
    }
}

// The astute reader may note that this allows a race.  I know
// of no way of avoiding it with signal semantics.

void signalHandler(int)
{
    if (Locked)
    {
        unlink("/etc/mtab~");
    }
    exit(1);
}


int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s [-ro] type arg mountpoint\n", 
                argv[0]);
        return 1;
    }
    // check if vfs driver loaded and iwserver running
    int fd = open("/proc/fs/pmfs", 2 /* O_RDONLY */);
    if (fd < 0)
    {
        fprintf(stderr,"%s ", argv[0]);
        perror("couldn't open /proc/fs/pmfs\n");
        return 1;
    }

    // see if we are already mounted
    int result = ioctl(fd, UVFS_IOCTL_MOUNT);
    if (result)
    {
        fprintf(stderr,"%s %s %s %s", argv[0], argv[1], argv[2], argv[3]);
        fprintf(stderr," already mounted on /proc/fs/pmfs\n");
        close(fd);
        return 0;
    }

    // see how many opens there are
    result = ioctl(fd, UVFS_IOCTL_USE_COUNT);
    if (result < 0)
    {
        fprintf(stderr,"%s ", argv[0]);
        perror("ioctl UVFS_IOCTL_USE_COUNT failed on /proc/fs/pmfs\n");
        close(fd);
        return 1;
    }
    close(fd);

    // check if iwserver is running
    if(result < 2)
    {
        fprintf(stderr,"%s ", argv[0]);
        fprintf(stderr,"iwserver does not appear to be running\n");
        return 1;
    }

    unsigned long flags = MS_MGC_VAL;
    int i;
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-ro") == 0)
        {
            flags |= MS_RDONLY;
        }
        else
        {
            break;
        }
    }
    if (i < argc - 3)
    {
        fprintf(stderr, "usage: %s [-ro|-nosuid] type arg mountpoint\n", 
                argv[0]);
    }
    char opts[128];
    sprintf(opts, "%s", (flags & MS_RDONLY) != 0 ? "ro" : "rw");
    char buff[4096];
    strncpy(buff, argv[i + 1], 4095);
    buff[4095] = 0;
    signalHandleAll(signalHandler);
    if (mount("none",  argv[i + 2], argv[i], flags, buff) == -1)
    {
        perror("Failed to mount");
        return 1;
    }
    if (!lock_mtab())
    {
        fprintf(stderr, "Couldn't lock /etc/mtab\n");
        return 0;
    }
    FILE* mnt = setmntent("/etc/mtab", "a");
    if (mnt == 0)
    {
        fprintf(stderr, "Could not open /etc/mtab\n");
        return 0;
    }
    struct mntent entry;
    entry.mnt_fsname = argv[i + 1];
    entry.mnt_dir = argv[i + 2];
    entry.mnt_type = argv[i];
    entry.mnt_opts = opts;
    entry.mnt_freq = 0;
    entry.mnt_passno = 0;
    addmntent(mnt, &entry);
    endmntent(mnt);
    unlock_mtab();
    return 0;
}
