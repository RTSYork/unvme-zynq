/**
 * Copyright (c) 2015-2016, Micron Technology, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief UNVMe API test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <err.h>

#include "unvme.h"

/// Print if verbose flag is set
#define VERBOSE(fmt, arg...) if (verbose) printf(fmt, ##arg)


/**
 * Main.
 */
int main(int argc, char** argv)
{
    const char* usage = "Usage: %s [OPTION]... PCINAME\n\
           -v         verbose\n\
           -r RATIO   max blocks per I/O ratio (default 4)\n\
           PCINAME    PCI device name (as 01:00.0[/1] format)";

    int opt, ratio=4, verbose=0;
    const char* prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];

    while ((opt = getopt(argc, argv, "r:v")) != -1) {
        switch (opt) {
        case 'r':
            ratio = strtol(optarg, 0, 0);
            if (ratio <= 0) errx(1, "r must be > 0");
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            warnx(usage, prog);
            exit(1);
        }
    }
    if ((optind + 1) != argc) {
        warnx(usage, prog);
        exit(1);
    }
    char* pciname = argv[optind];

    printf("API TEST BEGIN\n");
    const unvme_ns_t* ns = unvme_open(pciname);
    if (!ns) exit(1);

    // set large number of I/O and size
    int maxnlb = ratio * ns->maxbpio;
    int iocount = ratio * (ns->qsize - 1);

    printf("%s qc=%d/%d qs=%d/%d bc=%#lx bs=%d maxnlb=%d/%d\n",
            ns->device, ns->qcount, ns->maxqcount, ns->qsize, ns->maxqsize,
            ns->blockcount, ns->blocksize, maxnlb, ns->maxbpio);

    int q, i, nlb;
    u64 slba, size, w, *p;
    unvme_iod_t* iod = malloc(iocount * sizeof(unvme_iod_t));
    void** buf = malloc(iocount * sizeof(void*));

    time_t tstart = time(0);
    for (q = 0; q < ns->qcount; q++) {
        printf("> Test q=%d ioc=%d\n", q, iocount);
        int t = time(0);

        printf("Test alloc\n");
        srandom(t);
        for (i = 0; i < iocount; i++) {
            nlb = random() % maxnlb + 1;
            size = nlb * ns->blocksize;
            VERBOSE("  alloc.%-2d  %#8x %#lx\n", i, nlb, size);
            if (!(buf[i] = unvme_alloc(ns, size)))
                errx(1, "alloc.%d failed", i);
        }

        printf("Test awrite\n");
        srandom(t);
        slba = 0;
        for (i = 0; i < iocount; i++) {
            nlb = random() % maxnlb + 1;
            size = nlb * ns->blocksize / sizeof(u64);
            p = buf[i];
            for (w = 0; w < size; w++) p[w] = (w << 32) + i;
            VERBOSE("  awrite.%-2d %#8x %p %#lx\n", i, nlb, p, slba);
            if (!(iod[i] = unvme_awrite(ns, q, p, slba, nlb)))
                errx(1, "awrite.%d failed", i);
            slba += nlb;
        }

        printf("Test apoll.awrite\n");
        for (i = iocount-1; i >= 0; i--) {
            VERBOSE("  apoll.awrite.%-2d\n", i);
            if (unvme_apoll(iod[i], UNVME_TIMEOUT))
                errx(1, "apoll_awrite.%d failed", i);
        }

        printf("Test aread\n");
        srandom(t);
        slba = 0;
        for (i = 0; i < iocount; i++) {
            nlb = random() % maxnlb + 1;
            size = nlb * ns->blocksize;
            p = buf[i];
            for (int i = 0; i < size/sizeof(u64); i++) p[i] = 0;
            VERBOSE("  aread.%-2d  %#8x %p %#lx\n", i, nlb, p, slba);
            if (!(iod[i] = unvme_aread(ns, q, p, slba, nlb)))
                errx(1, "aread.%d failed", i);
            slba += nlb;
        }

        printf("Test apoll.aread\n");
        for (i = iocount-1; i >= 0; i--) {
            VERBOSE("  apoll.aread.%-2d\n", i);
            if (unvme_apoll(iod[i], UNVME_TIMEOUT))
                errx(1, "apoll_aread.%d failed", i);
        }

        printf("Test verify\n");
        srandom(t);
        slba = 0;
        for (i = 0; i < iocount; i++) {
            nlb = random() % maxnlb + 1;
            size = nlb * ns->blocksize / sizeof(u64);
            p = buf[i];
            VERBOSE("  verify.%-2d %#8x %p %#lx\n", i, nlb, p, slba);
            for (w = 0; w < size; w++) {
                if (p[w] != ((w << 32) + i)) {
                    w *= sizeof(w);
                    slba += w / ns->blocksize;
                    w %= ns->blocksize;
                    errx(1, "miscompare at lba %#lx offset %#lx", slba, w);
                }
            }
            slba += nlb;
        }

        printf("Test free\n");
        for (i = 0; i < iocount; i++) {
            VERBOSE("  free.%-2d\n", i);
            if (unvme_free(ns, buf[i]))
                errx(1, "free.%d failed", i);
        }
    }

    free(buf);
    free(iod);
    unvme_close(ns);

    printf("API TEST COMPLETE (%ld secs)\n", time(0) - tstart);
    return 0;
}
