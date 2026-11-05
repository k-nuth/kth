// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

// Test interposition only. The one-shot flag publishes the pipe descriptors;
// actual coordination uses blocking pipes, never timing or shared-state polling.
static atomic_int armed;
static int entered_fd, release_fd;
static struct stat target;
void kth_arm_block_read_gate(char const *path) {
    if (stat(path, &target) != 0) abort();
    entered_fd = atoi(getenv("KTH_BLOCK_READ_ENTERED_FD"));
    release_fd = atoi(getenv("KTH_BLOCK_READ_RELEASE_FD"));
    atomic_store_explicit(&armed, 1, memory_order_release);
}
size_t fread(void *ptr, size_t size, size_t count, FILE *file) {
    size_t (*original)(void *, size_t, size_t, FILE *) = dlsym(RTLD_NEXT, "fread");
    struct stat current;
    if (atomic_load_explicit(&armed, memory_order_acquire) &&
        fstat(fileno(file), &current) == 0 && current.st_dev == target.st_dev && current.st_ino == target.st_ino &&
        atomic_exchange_explicit(&armed, 0, memory_order_acquire)) {
        char released;
        ssize_t result;
        do { result = write(entered_fd, "R", 1); } while (result < 0 && errno == EINTR);
        do { result = read(release_fd, &released, 1); } while (result < 0 && errno == EINTR);
    }
    return original(ptr, size, count, file);
}
