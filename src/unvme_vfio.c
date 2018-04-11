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
 * @brief VFIO support routines.
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <linux/pci.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>

#include "unvme_vfio.h"
#include "unvme_log.h"

/// Print fatal error and exit
#define FATAL(fmt, arg...)  do { ERROR(fmt, ##arg); abort(); } while (0)

/// Starting device DMA address
#define UIO_BASE 0x40000000
/// Size of UIO buffer/device
#define UIO_SIZE 0x40000000

/// IRQ index names
const char* vfio_irq_names[] = { "INTX", "MSI", "MSIX", "ERR", "REQ" };


/**
 * Read a vfio device.
 * @param   dev         device context
 * @param   buf         buffer to read into
 * @param   len         read size
 * @param   off         offset
 */
static void vfio_read(vfio_device_t* dev, void* buf, size_t len, off_t off)
{
    if (pread(dev->fd, buf, len, off) != len)
        FATAL("pread(off=%#lx len=%#lx)", off, len);
}

/**
 * Write to vfio device.
 * @param   dev         device context
 * @param   buf         buffer to read into
 * @param   len         read size
 * @param   off         offset
 */
static void vfio_write(vfio_device_t* dev, void* buf, size_t len, off_t off)
{
    if (pwrite(dev->fd, buf, len, off) != len)
        FATAL("pwrite(off=%#lx len=%#lx)", off, len);
}

/**
 * Allocate VFIO memory.  The size will be rounded to page aligned size.
 * If pmb is set, it indicates memory has been premapped.
 * @param   dev         device context
 * @param   size        size
 * @param   pmb         premapped buffer
 * @return  memory structure pointer or NULL if error.
 */
static vfio_mem_t* vfio_mem_alloc(vfio_device_t* dev, size_t size, void* pmb)
{
    vfio_mem_t* mem = zalloc(sizeof(*mem));
    mem->size = size;
    size_t mask = dev->pagesize - 1;
    size = (size + mask) & ~mask;

    if (pmb) {
        mem->dma.buf = pmb;
    } else {
        if (dev->uiobufoff + size > UIO_SIZE) {
            ERROR("Out of UIO memory space (next allocation would use %#lx of %#lx)", dev->uiobufoff + size, UIO_SIZE);
            free(mem);
            return NULL;
        }
        mem->dma.buf = dev->uiobuf + dev->uiobufoff;
        dev->uiobufoff += size;
    }

    pthread_mutex_lock(&dev->lock);
    mem->dma.size = size;
    mem->dma.addr = dev->iovanext;
    mem->dma.mem = mem;
    mem->dev = dev;

    // add node to the memory list
    if (!dev->memlist) {
        mem->prev = mem;
        mem->next = mem;
        dev->memlist = mem;
    } else {
        mem->prev = dev->memlist->prev;
        mem->next = dev->memlist;
        dev->memlist->prev->next = mem;
        dev->memlist->prev = mem;
    }
    dev->iovanext += size;
    DEBUG_FN("%x %#lx %#lx %#lx %#lx", dev->pci, mem->dma.addr, size, dev->iovanext, dev->uiobufoff);
    pthread_mutex_unlock(&dev->lock);

    return mem;
}

/**
 * Free up VFIO memory.
 * @param   mem         memory pointer
 * @return  0 if ok else -1.
 */
int vfio_mem_free(vfio_mem_t* mem)
{
    vfio_device_t* dev = mem->dev;

    // remove node from memory list
    pthread_mutex_lock(&dev->lock);
    if (mem->next == dev->memlist) { // If removing last item in list
        dev->iovanext -= mem->dma.size;
        dev->uiobufoff -= mem->dma.size;
    }
    if (mem->next == mem) { // If removing only item in list
        dev->memlist = NULL;
        dev->iovanext = dev->iovabase;
        dev->uiobufoff = 0;
    } else { // If there are other items in list
        mem->next->prev = mem->prev;
        mem->prev->next = mem->next;
        if (dev->memlist == mem) { // If first item in list
            dev->memlist = mem->next;
        }
        dev->iovanext = dev->memlist->prev->dma.addr + dev->memlist->prev->dma.size; // IOVA next is after last item in list
        dev->uiobufoff = dev->iovanext - dev->iovabase; // UIO buffer offset is same as IOVA offset
    }
    DEBUG_FN("%x %#lx %#lx %#lx %#lx", dev->pci, mem->dma.addr, mem->dma.size, dev->iovanext, dev->uiobufoff);
    pthread_mutex_unlock(&dev->lock);

    free(mem);
    return 0;
}

/**
 * Map a premapped buffer and return a DMA buffer.
 * @param   dev         device context
 * @param   size        allocation size
 * @param   pmb         premapped buffer
 * @return  0 if ok else -1.
 */
vfio_dma_t* vfio_dma_map(vfio_device_t* dev, size_t size, void* pmb)
{
    vfio_mem_t* mem = vfio_mem_alloc(dev, size, pmb);
    return mem ? &mem->dma : NULL;
}

/**
 * Free a DMA buffer (without unmapping dma->buf).
 * @param   dma         memory pointer
 * @return  0 if ok else -1.
 */
int vfio_dma_unmap(vfio_dma_t* dma)
{
    return vfio_mem_free(dma->mem);
}

/**
 * Allocate and return a DMA buffer.
 * @param   dev         device context
 * @param   size        allocation size
 * @return  0 if ok else -1.
 */
vfio_dma_t* vfio_dma_alloc(vfio_device_t* dev, size_t size)
{
    vfio_mem_t* mem = vfio_mem_alloc(dev, size, 0);
    return mem ? &mem->dma : NULL;
}

/**
 * Free a DMA buffer.
 * @param   dma         memory pointer
 * @return  0 if ok else -1.
 */
int vfio_dma_free(vfio_dma_t* dma)
{
    return vfio_mem_free(dma->mem);
}

/**
 * Enable MSIX and map interrupt vectors to VFIO events.
 * @param   dev         device context
 * @param   start       first vector
 * @param   count       number of vectors to enable
 * @param   efds        event file descriptors
 */
void vfio_msix_enable(vfio_device_t* dev, int start, int count, __s32* efds)
{
    DEBUG_FN("%x start=%d count=%d", dev->pci, start, count);

    if (dev->msixsize == 0)
        FATAL("no MSIX support");
    if ((start + count) > dev->msixsize)
        FATAL("MSIX request %d exceeds limit %d", count, dev->msixsize);
    if (dev->msixnvec)
        FATAL("MSIX is already enabled");

    // if first time register all vectors else register specified vectors
    int len = sizeof(struct vfio_irq_set) + (count * sizeof(__s32));
    struct vfio_irq_set* irqs = zalloc(len);
    memcpy(irqs->data, efds, count * sizeof(__s32));
    irqs->argsz = len;
    irqs->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irqs->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irqs->start = start;
    irqs->count = count;
    HEX_DUMP(irqs, len);

    if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, irqs))
        FATAL("VFIO_DEVICE_SET_IRQS %d %d: %s", start, count, strerror(errno));

    dev->msixnvec += count;
    free(irqs);
}

/**
 * Disable MSIX interrupt.
 * @param   dev         device context
 */
void vfio_msix_disable(vfio_device_t* dev)
{
    if (dev->msixnvec == 0) return;

    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = VFIO_PCI_MSIX_IRQ_INDEX,
        .start = 0,
        .count = 0,
    };
    if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set))
        FATAL("VFIO_DEVICE_SET_IRQS 0 0: %s", strerror(errno));

    dev->msixnvec = 0;
}

/**
 * Create a VFIO device context.
 * @param   dev         if NULL then allocate context
 * @param   pci         PCI device id (as %x:%x.%x format)
 * @return  device context or NULL if failure.
 */
vfio_device_t* vfio_create(vfio_device_t* dev, int pci)
{
    // map PCI to vfio device number
    int i;
    char pciname[64];
    sprintf(pciname, "0000:%02x:%02x.%x", pci >> 16, (pci >> 8) & 0xff, pci & 0xff);

    char path[128];
    sprintf(path, "/sys/bus/pci/devices/%s/iommu_group", pciname);
    if ((i = readlink(path, path, sizeof(path))) < 0)
        FATAL("No iommu_group associated with device %s", pciname);
    path[i] = 0;
    sprintf(path, "/dev/vfio/noiommu-%s", &strrchr(path, '/')[1]);
    
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    struct vfio_device_info dev_info = { .argsz = sizeof(dev_info) };

    // allocate and initialize device context
    if (!dev) dev = zalloc(sizeof(*dev));
    else dev->ext = 1;
    dev->pci = pci;
    dev->pagesize = sysconf(_SC_PAGESIZE);
    dev->iovabase = UIO_BASE;
    dev->iovanext = dev->iovabase;
    if (pthread_mutex_init(&dev->lock, 0)) return NULL;

    // map vfio context
    if ((dev->contfd = open("/dev/vfio/vfio", O_RDWR)) < 0)
        FATAL("open /dev/vfio/vfio");

    if (ioctl(dev->contfd, VFIO_GET_API_VERSION) != VFIO_API_VERSION)
        FATAL("ioctl VFIO_GET_API_VERSION");

    if (ioctl(dev->contfd, VFIO_CHECK_EXTENSION, VFIO_NOIOMMU_IOMMU) == 0)
        FATAL("ioctl VFIO_CHECK_EXTENSION");

    if ((dev->groupfd = open(path, O_RDWR)) < 0)
        FATAL("open %s failed", path);

    if (ioctl(dev->groupfd, VFIO_GROUP_GET_STATUS, &group_status) < 0)
        FATAL("ioctl VFIO_GROUP_GET_STATUS");

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE))
        FATAL("group not viable %#x", group_status.flags);

    if (ioctl(dev->groupfd, VFIO_GROUP_SET_CONTAINER, &dev->contfd) < 0)
        FATAL("ioctl VFIO_GROUP_SET_CONTAINER");

    if (ioctl(dev->contfd, VFIO_SET_IOMMU, VFIO_NOIOMMU_IOMMU) < 0)
        FATAL("ioctl VFIO_SET_IOMMU");

    dev->fd = ioctl(dev->groupfd, VFIO_GROUP_GET_DEVICE_FD, pciname);
    if (dev->fd < 0)
        FATAL("ioctl VFIO_GROUP_GET_DEVICE_FD");

    if (ioctl(dev->fd, VFIO_DEVICE_GET_INFO, &dev_info) < 0)
        FATAL("ioctl VFIO_DEVICE_GET_INFO");

    DEBUG_FN("%x flags=%u regions=%u irqs=%u",
             pci, dev_info.flags, dev_info.num_regions, dev_info.num_irqs);

    for (i = 0; i < dev_info.num_regions; i++) {
        struct vfio_region_info reg = { .argsz = sizeof(reg), .index = i };

        if (ioctl(dev->fd, VFIO_DEVICE_GET_REGION_INFO, &reg)) continue;

        DEBUG_FN("%x region=%d flags=%#x off=%#llx size=%#llx",
                 pci, reg.index, reg.flags, reg.offset, reg.size);

        if (i == VFIO_PCI_CONFIG_REGION_INDEX) {
            __u8 config[256];
            vfio_read(dev, config, sizeof(config), reg.offset);
            HEX_DUMP(config, sizeof(config));

            __u16* vendor = (__u16*)(config + PCI_VENDOR_ID);
            __u16* cmd = (__u16*)(config + PCI_COMMAND);

            if (*vendor == 0xffff)
                FATAL("device in bad state");

            *cmd |= PCI_COMMAND_MASTER|PCI_COMMAND_MEMORY|PCI_COMMAND_INTX_DISABLE;
            vfio_write(dev, cmd, sizeof(*cmd), reg.offset + PCI_COMMAND);
            vfio_read(dev, cmd, sizeof(*cmd), reg.offset + PCI_COMMAND);

            // read MSIX table size
            __u8 cap = config[PCI_CAPABILITY_LIST];
            while (cap) {
                if (config[cap] == PCI_CAP_ID_MSIX) {
                    __u16* msixflags = (__u16*)(config + cap + PCI_MSIX_FLAGS);
                    dev->msixsize = (*msixflags & PCI_MSIX_FLAGS_QSIZE) + 1;
                    break;
                }
                cap = config[cap+1];
            }

            DEBUG_FN("%x vendor=%#x cmd=%#x msix=%d device=%#x rev=%d",
                     pci, *vendor, *cmd, dev->msixsize,
                     (__u16*)(config + PCI_DEVICE_ID), config[PCI_REVISION_ID]);
        }
    }

    for (i = 0; i < dev_info.num_irqs; i++) {
        struct vfio_irq_info irq = { .argsz = sizeof(irq), .index = i };

        if (ioctl(dev->fd, VFIO_DEVICE_GET_IRQ_INFO, &irq)) continue;
        DEBUG_FN("%x irq=%s count=%d flags=%#x",
                 pci, vfio_irq_names[i], irq.count, irq.flags);
        if (i == VFIO_PCI_MSIX_IRQ_INDEX && irq.count != dev->msixsize)
            FATAL("VFIO_DEVICE_GET_IRQ_INFO MSIX count %d != %d", irq.count, dev->msixsize);
    }

    // Open and map UIO device as memory buffer
    dev->uiofd = open("/dev/uio0", O_RDWR | O_SYNC);
    if (dev->uiofd == -1)
        FATAL("unable to open /dev/uio0, %d", errno);
    dev->uiobuf = mmap(NULL, UIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, dev->uiofd, 0);
    if (dev->uiobuf == MAP_FAILED)
        FATAL("unable to mmap /dev/uio0, %d", errno);

    if (mlock(dev->uiobuf, UIO_SIZE) == -1)
        FATAL("unable to mlock, %d", errno);

    return (vfio_device_t*)dev;
}

/**
 * Delete a VFIO device context.
 * @param   dev         device context
 */
void vfio_delete(vfio_device_t* dev)
{
    if (!dev) return;
    DEBUG_FN("%x", dev->pci);

    // Close and unmap UIO buffer
    munlock(dev->uiobuf, UIO_SIZE);
    munmap(dev->uiobuf, UIO_SIZE);
    close(dev->uiofd);

    // free all memory associated with the device
    while (dev->memlist) vfio_mem_free(dev->memlist);

    if (dev->fd) {
        close(dev->fd);
        dev->fd = 0;
    }
    if (dev->contfd) {
        close(dev->contfd);
        dev->contfd = 0;
    }
    if (dev->groupfd) {
        close(dev->groupfd);
        dev->groupfd = 0;
    }

    pthread_mutex_destroy(&dev->lock);
    if (!dev->ext) free(dev);
}
