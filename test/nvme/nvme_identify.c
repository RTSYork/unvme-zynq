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
 * @brief Invoke NVMe identify command.
 */

#include <string.h>
#include "nvme_common.c"


/**
 * Print controller info.
 */
void print_controller(void* buf)
{
    nvme_identify_ctlr_t* ctlr = buf;

    printf("Identify Controller 0x0\n");
    printf("=======================\n");
    printf("vid      : %#x\n", ctlr->vid);
    printf("ssvid    : %#x\n", ctlr->ssvid);
    printf("sn       : %.20s\n", ctlr->sn);
    printf("mn       : %.40s\n", ctlr->mn);
    printf("fr       : %.8s\n", ctlr->fr);
    printf("rab      : %#x\n", ctlr->rab);
    printf("ieee     : %02x%02x%02x\n", ctlr->ieee[0], ctlr->ieee[1], ctlr->ieee[2]);
    printf("mic      : %#x\n", ctlr->mic);
    printf("mdts     : %#x\n", ctlr->mdts);
    printf("oacs     : %#x\n", ctlr->oacs);
    printf("acl      : %#x\n", ctlr->acl);
    printf("aerl     : %#x\n", ctlr->aerl);
    printf("frmw     : %#x\n", ctlr->frmw);
    printf("lpa      : %#x\n", ctlr->lpa);
    printf("elpe     : %#x\n", ctlr->elpe);
    printf("npss     : %#x\n", ctlr->npss);
    printf("avscc    : %#x\n", ctlr->avscc);
    printf("sqes     : %#x\n", ctlr->sqes);
    printf("cqes     : %#x\n", ctlr->cqes);
    printf("nn       : %#x\n", ctlr->nn);
    printf("oncs     : %#x\n", ctlr->oncs);
    printf("fuses    : %#x\n", ctlr->fuses);
    printf("fna      : %#x\n", ctlr->fna);
    printf("vwc      : %#x\n", ctlr->vwc);
    printf("awun     : %#x\n", ctlr->awun);
    printf("awupf    : %#x\n", ctlr->awupf);
    printf("nvscc    : %#x\n", ctlr->nvscc);
}

/**
 * Print namespace info.
 */
void print_namespace(void* buf, int nsid)
{
    nvme_identify_ns_t* ns = buf;

    printf("\nIdentify Namespace %#x\n", nsid);
    printf("======================\n");
    printf("nsze     : %#lx\n", ns->nsze);
    printf("ncap     : %#lx\n", ns->ncap);
    printf("nuse     : %#lx\n", ns->nuse);
    printf("nsfeat   : %#x\n", ns->nsfeat);
    printf("nlbaf    : %#x\n", ns->nlbaf);
    printf("flbas    : %#x\n", ns->flbas);
    printf("mc       : %#x\n", ns->mc);
    printf("dpc      : %#x\n", ns->dpc);
    printf("dps      : %#x\n", ns->dps);

    int i;
    for (i = 0; i <= ns->nlbaf; i++) {
        printf("lbaf.%-3d : ms=%-3d lbads=%-3d rp=%-2d %s\n",
               i, ns->lbaf[i].ms, ns->lbaf[i].lbads, ns->lbaf[i].rp,
               (ns->flbas & 0xf) == i ? "(formatted)" : "");
    }
}

/**
 * Main program.
 */
int main(int argc, char* argv[])
{
    if (argc < 2) {
        warnx("Usage: %s PCINAME [NSID]", argv[0]);
        exit(1);
    }
    int nsid = 0;
    if (argc > 2) {
        nsid = strtol(argv[2], 0, 0);
        if (nsid < 1) errx(1, "invalid nsid %#x", nsid);
    }

    nvme_setup(argv[1], 8);
    vfio_dma_t* dma = vfio_dma_alloc(vfiodev, 16384);
    if (!dma) errx(1, "vfio_dma_alloc");

    if (nvme_acmd_identify(nvmedev, 0, dma->addr, dma->addr + 4096))
        errx(1, "nvme_acmd_identify 0");
    nvme_identify_ctlr_t* ctlr = malloc(sizeof(nvme_identify_ctlr_t));
    memcpy(ctlr, dma->buf, sizeof(nvme_identify_ctlr_t));
    print_controller(ctlr);

    u64 nsaddr = dma->addr + 8192;
    void* nsbuf = dma->buf + 8192;

    if (nsid) {
        if (nsid > ctlr->nn)
            errx(1, "invalid nsid %d", nsid);
        if (nvme_acmd_identify(nvmedev, nsid, nsaddr, nsaddr + 4096))
            errx(1, "nvme_acmd_identify %d", nsid);
        print_namespace(nsbuf + 8192, nsid);
    } else {
        for (nsid = 1; nsid <= ctlr->nn; nsid++) {
            if (nvme_acmd_identify(nvmedev, nsid, nsaddr, nsaddr + 4096))
                errx(1, "nvme_acmd_identify %d", nsid);
            print_namespace(nsbuf + 8192, nsid);
        }
    }

    free(ctlr);
    vfio_dma_free(dma);
    nvme_cleanup();

    return 0;
}

