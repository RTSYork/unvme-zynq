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
 * @brief UNVMe multi-threaded/session test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <time.h>
#include <err.h>

#include "unvme.h"

// Global variables
static int numses = 4;          ///< number of thread sessions
static int qcount = 2;          ///< number of queues per session
static int maxnlb = 32;         ///< maximum number of blocks per IO
static sem_t sm_ready;          ///< semaphore for ready
static sem_t sm_start;          ///< semaphore for start
static const unvme_ns_t* ns;    ///< driver namespace handle

/// thread session arguments
typedef struct {
    int                 id;     ///< session id
    int                 qid;    ///< starting queue id
    u64                 slba;   ///< starting lba
} ses_arg_t;


/**
 * Thread per queue.
 */
void* test_queue(void* arg)
{
    ses_arg_t* ses = arg;
    u64 slba, wlen, w, *p;
    int nlb, l, i;

    printf("Test s%d q%-2d lba %#lx started\n", ses->id, ses->qid, ses->slba);
    sem_post(&sm_ready);
    sem_wait(&sm_start);

    unvme_iod_t* iod = calloc(ns->qsize, sizeof(unvme_iod_t));
    void** buf = calloc(ns->qsize, sizeof(void*));
    int* buflen = calloc(ns->qsize, sizeof(int));

    for (l = 0; l < numses; l++) {
        // allocate buffers
        for (i = 0; i < ns->qsize; i++) {
            nlb = random() % maxnlb + 1;
            buflen[i] = nlb * ns->blocksize;
            if (!(buf[i] = unvme_alloc(ns, buflen[i])))
                errx(1, "alloc.%d.%d.%d failed", ses->id, ses->qid, i);
        }

#ifdef DO_SYNC_WRITE_READ
        slba = ses->slba;
        for (i = 0; i < ns->qsize; i++) {
            nlb = buflen[i] / ns->blocksize;
            wlen = buflen[i] / sizeof (u64);
            p = buf[i];
            for (w = 0; w < wlen; w++) p[w] = (w << 32) + i;
            if (unvme_write(ns, ses->qid, p, slba, nlb))
                errx(1, "write.%d.%d.%d failed", ses->id, ses->qid, i);
            bzero(p, buflen[i]);
            if (unvme_read(ns, ses->qid, p, slba, nlb))
                errx(1, "read.%d.%d.%d failed", ses->id, ses->qid, i);
            for (w = 0; w < wlen; w++) {
                if (p[w] != ((w << 32) + i))
                    errx(1, "data.%d.%d.%d error", ses->id, ses->qid, i);
            }
            slba += nlb;
        }
#else
        // async write
        slba = ses->slba;
        for (i = 0; i < ns->qsize; i++) {
            nlb = buflen[i] / ns->blocksize;
            wlen = buflen[i] / sizeof (u64);
            p = buf[i];
            for (w = 0; w < wlen; w++) p[w] = (w << 32) + i;
            if (!(iod[i] = unvme_awrite(ns, ses->qid, p, slba, nlb)))
                errx(1, "awrite.%d.%d.%d failed", ses->id, ses->qid, i);
            slba += nlb;
        }

        // async poll to complete all writes
        for (i = 0; i < ns->qsize; i++) {
            if (unvme_apoll(iod[i], UNVME_TIMEOUT))
                errx(1, "apoll.%d.%d.%d failed", ses->id, ses->qid, i);
        }

        // do sync read and compare
        slba = ses->slba;
        for (i = 0; i < ns->qsize; i++) {
            nlb = buflen[i] / ns->blocksize;
            wlen = buflen[i] / sizeof (u64);
            p = buf[i];
            for (int i = 0; i < buflen[i]/sizeof(u64); i++) p[i] = 0;
            if (unvme_read(ns, ses->qid, p, slba, nlb))
                errx(1, "read.%d.%d.%d failed", ses->id, ses->qid, i);
            for (w = 0; w < wlen; w++) {
                if (p[w] != ((w << 32) + i))
                    errx(1, "data.%d.%d.%d error", ses->id, ses->qid, i);
            }
            slba += nlb;
        }
#endif

        // free buffers
        for (i = 0; i < ns->qsize; i++) {
            if (unvme_free(ns, buf[i]))
                errx(1, "free failed");
        }
    }

    free(buflen);
    free(buf);
    free(iod);
    printf("Test s%d q%-2d lba %#lx completed\n", ses->id, ses->qid, ses->slba);

    return 0;
}

/**
 * Thread per session.
 */
void* test_session(void* arg)
{
    int sesid = (long)arg;
    int sid = sesid + 1;

    printf("Session %d started\n", sid);
    sem_post(&sm_ready);
    sem_wait(&sm_start);

    u64 bpq = ns->blockcount / numses / qcount;
    pthread_t* sqt = calloc(qcount, sizeof(pthread_t));
    ses_arg_t* sarg = calloc(qcount, sizeof(ses_arg_t));

    int q;
    for (q = 0; q < qcount; q++) {
        sarg[q].id = sid;
        sarg[q].qid = q + sesid * qcount;
        sarg[q].slba = bpq * (sesid * qcount + q);
        pthread_create(&sqt[q], 0, test_queue, &sarg[q]);
    }
    for (q = 0; q < qcount; q++) sem_post(&sm_start);
    for (q = 0; q < qcount; q++) pthread_join(sqt[q], 0);

    free(sarg);
    free(sqt);
    printf("Session %d completed\n", sid);

    return 0;
}

/**
 * Main program.
 */
int main(int argc, char* argv[])
{
    const char* usage = "Usage: %s [OPTION]... PCINAME\n\
           -t THREADS  number of thread sessions (default 4)\n\
           -q QCOUNT   number of queues per session (default 2)\n\
           -m MAXNLB   maximum number of blocks per I/O (default 32)\n\
           PCINAME     PCI device name (as 01:00.0[/1] format)";

    char* prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];

    int opt, i;
    while ((opt = getopt(argc, argv, "t:q:m:")) != -1) {
        switch (opt) {
        case 't':
            numses = strtol(optarg, 0, 0);
            if (numses <= 0) errx(1, "t must be > 0");
            break;
        case 'q':
            qcount = strtol(optarg, 0, 0);
            if (qcount <= 0) errx(1, "t must be > 0");
            break;
        case 'm':
            maxnlb = strtol(optarg, 0, 0);
            if (maxnlb <= 0) errx(1, "m must be > 0");
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

    printf("MULTI-SESSION TEST BEGIN\n");
    if (!(ns = unvme_open(pciname))) exit(1);
    if ((numses * qcount) > ns->maxqcount)
        errx(1, "%d threads %d queues each exceeds limit of %d queues",
              numses, qcount, ns->maxqcount);
    printf("%s ses=%d qc=%d/%d qs=%d/%d bc=%#lx bs=%d maxnlb=%d/%d\n",
            ns->device, numses, qcount, ns->qcount, ns->qsize, ns->maxqsize,
            ns->blockcount, ns->blocksize, maxnlb, ns->maxbpio);

    if ((u64)(numses * qcount * ns->qsize * maxnlb) > ns->blockcount)
        errx(1, "not enough disk space");

    sem_init(&sm_ready, 0, 0);
    sem_init(&sm_start, 0, 0);
    pthread_t* st = calloc(numses * qcount, sizeof(pthread_t));

    time_t tstart = time(0);
    srandom(tstart);

    for (i = 0; i < numses; i++) {
        pthread_create(&st[i], 0, test_session, (void*)(long)i);
        sem_wait(&sm_ready);
    }
    for (i = 0; i < numses; i++) sem_post(&sm_start);
    for (i = 0; i < numses; i++) pthread_join(st[i], 0);

    sem_destroy(&sm_start);
    sem_destroy(&sm_ready);
    free(st);

    printf("MULTI-SESSION TEST COMPLETE (%ld secs)\n", time(0) - tstart);
    return 0;
}

