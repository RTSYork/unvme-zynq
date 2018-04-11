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
 * @brief UNVMe simple write-read-verify test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <err.h>

#include "unvme.h"


int main(int argc, char** argv)
{
    const char* usage = "Usage: %s [OPTION]... PCINAME\n\
           -a LBA     use starting LBA (default random)\n\
           -s SIZE    data size (default 100M)\n\
           PCINAME    PCI device name (as 01:00.0[/1] format)";

    int opt;
    u64 datasize = 100 * 1024 * 1024;
    const char* prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];
    char* endp;
    u64 slba = -1L;

    while ((opt = getopt(argc, argv, "s:a:")) != -1) {
        switch (opt) {
        case 'a':
            slba = strtoull(optarg, 0, 0);
            break;
        case 's':
            datasize = strtoull(optarg, &endp, 0);
            if (endp) {
                int c = tolower(*endp);
                if (c == 'k') datasize *= 1024;
                else if (c == 'm') datasize *= 1024 * 1024;
                else if (c == 'g') datasize *= 1024 * 1024 * 1024;
            }
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

    printf("SIMPLE WRITE-READ-VERIFY TEST BEGIN\n");
    const unvme_ns_t* ns = unvme_open(pciname);
    if (!ns) exit(1);
    printf("%s qc=%d/%d qs=%d/%d bc=%#lx bs=%d mbio=%d ds=%#lx\n",
            ns->device, ns->qcount, ns->maxqcount, ns->qsize, ns->maxqsize,
            ns->blockcount, ns->blocksize, ns->maxbpio, datasize);

    void* buf = unvme_alloc(ns, datasize);
    if (!buf) errx(1, "unvme_alloc %ld failed", datasize);
    time_t tstart = time(0);
    u64 nlb = datasize / ns->blocksize;
    if (!nlb) nlb = 1;

    if (slba == -1L) {
        srandom(time(0));
        slba = (random() % ns->blockcount) - (ns->qcount * nlb);
        slba &= ~(ns->nbpp - 1);
        if (slba >= ns->blockcount) slba = 0;
    }

    u64* p = buf;
    u64 wsize = datasize / sizeof(u64);
    u64 w;
    int q, stat;

    for (q = 0; q < ns->qcount; q++) {
        printf("Test q=%-2d lba=%#lx nlb=%#lx\n", q, slba, nlb);
        for (w = 0; w < wsize; w++) {
            u64 pat = (q << 24) + w;
            p[w] = (pat << 32) | (~pat & 0xffffffff);
        }
        stat = unvme_write(ns, q, p, slba, nlb);
        if (stat)
            errx(1, "unvme_write failed: slba=%#lx nlb=%#lx stat=%#x", slba, nlb, stat);
        for (int i = 0; i < (nlb * ns->blocksize) / sizeof(u64); i++) p[i] = 0;
        stat = unvme_read(ns, q, p, slba, nlb);
        if (stat)
            errx(1, "unvme_read failed: slba=%#lx nlb=%#lx stat=%#x", slba, nlb, stat);
        for (w = 0; w < wsize; w++) {
            u64 pat = (q << 24) + w;
            pat = (pat << 32) | (~pat & 0xffffffff);
            if (p[w] != pat) {
                w *= sizeof(w);
                slba += w / ns->blocksize;
                w %= ns->blocksize;
                errx(1, "miscompare at lba %#lx offset %#lx", slba, w);
            }
        }
        slba += nlb;
        if (slba >= ns->blockcount) slba = 0;
    }

    unvme_free(ns, buf);
    unvme_close(ns);

    printf("SIMPLE WRITE-READ-VERIFY TEST COMPLETE (%ld secs)\n", time(0) - tstart);
    return 0;
}

