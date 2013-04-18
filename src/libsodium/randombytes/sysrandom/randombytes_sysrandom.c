
#include <sys/types.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#ifndef _WIN32
# include <poll.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "randombytes.h"
#include "randombytes_sysrandom.h"

#ifdef _WIN32
# include <Windows.h>
# include <Wincrypt.h>
#endif

typedef struct SysRandom_ {
#ifdef _WIN32
    HCRYPTPROV hcrypt_prov;
#endif
    int        random_data_source_fd;
    bool       initialized;
} SysRandom;

static SysRandom stream = {
    .random_data_source_fd = -1,
    .initialized = 0
};

#ifndef _WIN32
static ssize_t
safe_read(const int fd, void * const buf_, size_t count)
{
    unsigned char *buf = (unsigned char *) buf_;
    ssize_t        readnb;

    do {
        while ((readnb = read(fd, buf, count)) < (ssize_t) 0 &&
               errno == EINTR);
        if (readnb < (ssize_t) 0) {
            return readnb;
        }
        if (readnb == (ssize_t) 0) {
            break;
        }
        count -= (size_t) readnb;
        buf += readnb;
    } while (count > (ssize_t) 0);

    return (ssize_t) (buf - (unsigned char *) buf_);
}
#endif

#ifndef _WIN32
static int
randombytes_sysrandom_random_dev_open(void)
{
    static const char * const devices[] = {
# ifndef USE_BLOCKING_RANDOM
        "/dev/arandom", "/dev/urandom",
# endif
        "/dev/random", NULL
    };
    const char * const *device = devices;

    do {
        if (access(*device, F_OK | R_OK) == 0) {
            return open(*device, O_RDONLY);
        }
        device++;
    } while (*device != NULL);

    return -1;
}

static void
randombytes_sysrandom_init(void)
{
    if ((stream.random_data_source_fd =
         randombytes_sysrandom_random_dev_open()) == -1) {
        abort();
    }
}

#else /* _WIN32 */

static void
randombytes_sysrandom_init(void)
{
    if (! CryptAcquireContext(&stream.hcrypt_prov, NULL, NULL,
                              PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        abort();
    }
}
#endif

void
randombytes_sysrandom_stir(void)
{
    if (stream.initialized == 0) {
        randombytes_sysrandom_init();
        stream.initialized = 1;
    }
}

static void
randombytes_sysrandom_stir_if_needed(void)
{
    if (stream.initialized == 0) {
        randombytes_sysrandom_stir();
    }
}

int
randombytes_sysrandom_close(void)
{
    int ret = -1;

#ifndef _WIN32
    if (stream.random_data_source_fd != -1 &&
        close(stream.random_data_source_fd) == 0) {
        stream.random_data_source_fd = -1;
        stream.initialized = 0;
        ret = 0;
    }
#else /* _WIN32 */
    if (stream.initialized != 0 &&
        CryptReleaseContext(stream.hcrypt_prov, 0)) {
        stream.initialized = 0;
        ret = 0;
    }
#endif
    return ret;
}

uint32_t
randombytes_sysrandom(void)
{
    uint32_t r;

    randombytes_sysrandom_stir_if_needed();
    randombytes_sysrandom_buf(&r, sizeof r);

    return r;
}

void
randombytes_sysrandom_buf(void * const buf, const size_t size)
{
    randombytes_sysrandom_stir_if_needed();
#ifdef ULONG_LONG_MAX
    assert(size <= ULONG_LONG_MAX);
#endif
#ifndef _WIN32    
    if (safe_read(stream.random_data_source_fd, buf, size) != (ssize_t) size) {
        abort();
    }
#else
    if (! CryptGenRandom(stream.hcrypt_prov, size, buf)) {
        abort();
    }
#endif
}

/*
 * randombytes_sysrandom_uniform() derives from OpenBSD's arc4random_uniform()
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 */

uint32_t
randombytes_sysrandom_uniform(const uint32_t upper_bound)
{
    uint32_t min;
    uint32_t r;

    if (upper_bound < 2) {
        return 0;
    }
    min = (uint32_t) (-upper_bound % upper_bound);
    for (;;) {
        r = randombytes_sysrandom();
        if (r >= min) {
            break;
        }
    }
    return r % upper_bound;
}

const char *
randombytes_sysrandom_implementation_name(void)
{
    return "sysrandom";
}

struct randombytes_implementation
randombytes_sysrandom_implementation(void)
{
    return (randombytes_implementation) {
        .implementation_name = randombytes_sysrandom_implementation_name,
        .random = randombytes_sysrandom,
        .stir = randombytes_sysrandom_stir,
        .uniform = randombytes_sysrandom_uniform,
        .buf = randombytes_sysrandom_buf,
        .close = randombytes_sysrandom_close
    };
}