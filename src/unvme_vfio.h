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
 * @brief VFIO function support header files.
 */

#ifndef _UNVME_VFIO_H
#define _UNVME_VFIO_H

#include <stdlib.h>
#include <pthread.h>
#include <linux/vfio.h>

/// VFIO dma allocation structure
typedef struct _vfio_dma {
    void*                   buf;        ///< memory buffer
    size_t                  size;       ///< allocated size
    __u64                   addr;       ///< I/O DMA address
    struct _vfio_mem*       mem;        ///< private mem
} vfio_dma_t;

/// VFIO memory allocation entry
typedef struct _vfio_mem {
    struct _vfio_device*    dev;        ///< device owner
    int                     mmap;       ///< mmap indication flag
    vfio_dma_t              dma;        ///< dma mapped memory
    size_t                  size;       ///< size
    struct _vfio_mem*       prev;       ///< previous entry
    struct _vfio_mem*       next;       ///< next entry
} vfio_mem_t;

/// VFIO device structure
typedef struct _vfio_device {
    int                     pci;        ///< PCI device number
    int                     fd;         ///< device descriptor
    int                     groupfd;    ///< group file descriptor
    int                     contfd;     ///< container file descriptor
    int                     msixsize;   ///< max MSIX table size
    int                     msixnvec;   ///< number of enabled MSIX vectors
    int                     pagesize;   ///< system page size
    int                     ext;        ///< externally allocated flag
    __u64                   iovabase;   ///< IO virtual address base
    __u64                   iovanext;   ///< next IO virtual address to use
    __u64                   iovamask;   ///< max IO virtual address mask
    pthread_mutex_t         lock;       ///< multithreaded lock
    vfio_mem_t*             memlist;    ///< memory allocated list
    int                     uiofd;      ///< file descriptor of UIO device
    void*                   uiobuf;     ///< UIO buffer pointer
    off_t                   uiobufoff;  ///< UIO buffer offset
} vfio_device_t;

// Export functions
vfio_device_t* vfio_create(vfio_device_t* dev, int pci);
void vfio_delete(vfio_device_t* dev);
void vfio_msix_enable(vfio_device_t* dev, int start, int nvec, __s32* efds);
void vfio_msix_disable(vfio_device_t* dev);
int vfio_mem_free(vfio_mem_t* mem);
vfio_dma_t* vfio_dma_map(vfio_device_t* dev, size_t size, void* pmb);
int vfio_dma_unmap(vfio_dma_t* dma);
vfio_dma_t* vfio_dma_alloc(vfio_device_t* dev, size_t size);
int vfio_dma_free(vfio_dma_t* dma);

#endif // _UNVME_VFIO_H

