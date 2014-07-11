#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define TESTDIR "/tmp"
#define N1      "f1"

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;

    if (!ok) {
        printf("%s: FAIL (%s)\n", msg, strerror(errno));
    } else {
        printf("%s: PASS\n", msg);
    }
    if (fails) {
        exit(0);
    }
}

int main()
{
    int fd;

    report(chdir(TESTDIR) == 0, "chdir");

    fd = creat(N1, 0777);
    report(fd >= 0, "creat");

    DIR *d = opendir(TESTDIR);
    fd = dirfd(d);

    int fd1 = openat(fd, N1, O_RDONLY);
    remove(N1);
    report(fd1 >= 0, "openat");

    close(fd1);
    closedir(d);

    d = opendir("/dev/");
    fd = dirfd(d);
    fd1 = openat(fd, "random", O_RDONLY);
    report(fd1 >= 0, "openat");

    close(fd1);
    closedir(d);

    report(mkdir("/tmp/A", 0777) == 0, "mkdir");
    d = opendir("/tmp/A");
    report(d != NULL, "opendir");

    fd = dirfd(d);
    report(fd >= 0, "dirfd");

    fd1 = openat(fd, "1", O_RDONLY | O_CREAT, 0777);
    report(fd1 >= 0, "openat");

    close(fd1);
    closedir(d);

    fd1 = openat(AT_FDCWD, "A/1", O_RDONLY);
    report(fd1 >= 0, "openat");
    close(fd1);

    fd1 = openat(-1, "/tmp/A/1", O_RDONLY);
    report(fd1 >= 0, "openat");
    close(fd1);

    remove("/tmp/A/1");
    remove("/tmp/A");
    return 0;
}
