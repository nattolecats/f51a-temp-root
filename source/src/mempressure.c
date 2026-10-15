#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mempressure.h"

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

int mempressure_main(int argc, char **argv)
{
    size_t mib = argc > 1 ? strtoull(argv[1], NULL, 0) : 2048;
    size_t len = mib * 1024ULL * 1024ULL;
    volatile unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap(%zu MiB): %s\n", mib, strerror(errno));
        return 1;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    for (size_t off = 0; off < len; off += 4096) {
        p[off] = (unsigned char)(off >> 12);
        if ((off & ((256ULL << 20) - 1)) == 0) {
            fprintf(stderr, "touched=%zu/%zu MiB\n", off >> 20, mib);
            fflush(stderr);
        }
    }
    fprintf(stderr, "READY pid=%d size=%zu MiB\n", getpid(), mib);
    fflush(stderr);
    while (!stop) {
        for (size_t off = 0; off < len; off += 4096 * 256)
            p[off]++;
        sleep(1);
    }
    munmap((void *)p, len);
    return 0;
}
