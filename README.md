UNVMe - A User Space NVMe Driver
================================

UNVMe is a user space NVMe driver developed at Micron Technology.

**This version of the driver has been modified to work on the ARM64 processors
of the Xilinx Zynq UltraScale+ architecture, rather than UNVMe's original
target of x86-64. Note that instructions and requirements below are for the
original UNVMe project, and have not yet been updated to reflect the changes.**

The driver in this model is implemented as a library (libunvme.a) that
applications can be linked with.  Upon start, an application will first
initialize the NVMe device(s) and then, afterward, it can submit and process
I/O directly from the user space application to the device.

UNVMe application combines the application and driver into one program and
thus it takes complete ownership of a device upon execution, so an NVMe device
can only be accessed by one application at any given time.  However, a UNVMe
application can access multiple devices simultaneously.
Device name is to be specified in PCI format with optional NSID
(e.g. 0a:00.0 or 0a:00.0/1). NSID 1 is assumed if /NSID is omitted.

UNVMe applications must use the provided APIs instead of the typical system
POSIX APIs to access a device.


A design note:  UNVMe is designed modularly with independent components
including nvme (unvme_nvme.h) and vfio (unvme_vfio.h), in which the unvme 
interface is a module layered on top of the nvme and vfio modules.


The programs under test/nvme are built directly on nvme and vfio modules
without dependency on the unvme module.  They serve as examples for building
individual NVMe admin commands.

The programs under test/unvme are examples for developing applications using
the unvme interfaces (defined in unvme.h).  Note that vendor specific and NVMe
admin commands may also be built with the provided APIs.



System Requirements
===================

UNVMe has a dependency on features provided by the VFIO module in the Linux
kernel (introduced since 3.6).  UNVMe code has been built and tested only
with CentOS 6 and 7 running on x86_64 CPU based systems.

UNVMe requires the following hardware and software support:

    VT-d    -   CPU must support VT-d (Virtualization Technology for Directed I/O).
                Check <http://ark.intel.com/> for Intel product specifications.
                VT-d setting is normally found in the BIOS configuration.

    VFIO    -   Linux OS must have kernel 3.6 or later compiled with the
                following configurations:

                    CONFIG_IOMMU_API=y
                    CONFIG_IOMMU_SUPPORT=y
                    CONFIG_INTEL_IOMMU=y
                    CONFIG_VFIO=m
                    CONFIG_VFIO_PCI=m
                    CONFIG_VFIO_IOMMU_TYPE1=m

                The boot command line must set "intel_iommu=on" argument.

                To verify the system correctly configured with VFIO support,
                check that /sys/kernel/iommu_groups directory is not empty but
                contains other subdirectories (i.e. group numbers).

On CentOS 6, which comes with kernel version 2.x (i.e. prior to 3.6),
the user must compile and boot a newer kernel that has the VFIO module.
The user must also copy the header file from the kernel source directory
include/uapi/linux/vfio.h to /usr/include/linux if that is missing.

UNVMe requires root privilege to access a device.



Build, Run, and Test
====================

To download and install the driver library, run:

    $ git clone https://github.com/MicronSSD/unvme.git
    $ make install


To setup a device for UNVMe usage (do once before running applications), run:

    $ unvme-setup bind

    By default, all NVMe devices found in the system will be bound to the
    VFIO driver enabling them for UNVMe usage.  Specific PCI device(s)
    may also be specified for binding, e.g. unvme-setup bind 0a:00.0.


To reset device(s) to the NVMe kernel space driver, run:

    $ unvme-setup reset


For usage help, invoke unvme-setup without argument.


To run UNVMe tests, specify the device(s) with command:

    $ test/unvme-test 0a:00.0 0b:00.0


The commands under test/nvme may also be invoked individually, e.g.:

    $ test/nvme/nvme_identify 0a:00.0
    $ test/nvme/nvme_get_features 0a:00.0
    $ test/nvme/nvme_get_log_page 0a:00.0 1 2
    ...



Python Support
==============

Python dynamic library binding support is provided as unvme.py with examples
under the test/python directory.

    $ python test/python/unvme_info.py 0a:00.0
    $ python test/python/unvme_get_features.py 0a:00.0
    $ python test/python/unvme_wr_ex.py 0a:00.0


Note:  For Python scripts that reside elsewhere, you need to copy "libunvme.so"
and "unvme.py" to your working directory.  Alternatively, you can run
"make install" and export PYTHONPATH to where unvme.py module is.



I/O Benchmark Tests
===================

To run fio benchmark tests against UNVMe:

    1) Download and compile the fio source code (available on https://github.com/axboe/fio).


    2) Edit unvme/Makefile.def and set FIODIR to the compiled fio source
       directory (or alternatively export the FIODIR variable).


    3) Rerun make to include building the fio engine, since setting FIODIR
       will enable ioengine/unvme_fio to be built.
    
       $ make

       Note that the fio source code is constantly changing, and unvme_fio.c
       has been verified to work with the fio versions 2.7 through 2.19


    4) Set up for UNVMe driver (if not already):

       $ unvme-setup bind


    5) Launch the test script:
    
       $ test/unvme-benchmark DEVICENAME

       Note the benchmark test, by default, will run random write and read
       tests with 1, 4, 8, and 16 threads, and io depth of 1, 4, 8, and 16.
       Each test will be run for 120 seconds after a ramp time of 60 seconds.
       These default settings can be overridden from the shell command line, e.g.:

       $ RAMPTIME=10 RUNTIME=20 NUMJOBS="1 4" IODEPTH="4 8" test/unvme-benchmark 0a:00.0


To run the same tests against the kernel space driver:

    $ unvme-setup reset

    $ test/unvme-benchmark /dev/nvme0n1


All the FIO results, by default, will be stored in test/out directory.



Application Programming Interfaces
==================================

The UNVMe APIs are designed with application ease of use in mind.
As defined in unvme.h, the following functions are supported:

    unvme_open()     -  This function must be invoked first to establish a
                        connection to the specified PCI device.

    unvme_close()    -  Close a device connection.


    unvme_alloc()    -  Allocate an I/O buffer.

    unvme_free()     -  Free the allocated I/O buffer.


    unvme_write()    -  Write the specified number of blocks (nlb) to the
                        device starting at logical block address (slba).
                        The buffer must be acquired from unvme_alloc().
                        The qid (range from 0 to 1 less than the number of
                        queues supported by the device) may be used for
                        thread safe I/O operations.  Each queue must only
                        be accessed by a one thread at any one time.

    unvme_awrite()   -  Submit a write command to the device asynchronously
                        and return immediately.  The returned descriptor
                        is used via apoll() or apoll_cs() for completion.


    unvme_read()     -  Read from the device (i.e. like unvme_write).

    unvme_aread()    -  Submit an asynchronous read (i.e. like unvme_awrite).


    unvme_cmd()      -  Issue a generic or vendor specific command to 
                        the device.

    unvme_acmd()     -  Submit a generic or vendor specific command to
                        the device asynchronously and return immediately.
                        The returned descriptor is used via apoll() or
                        apoll_cs() for command completion.


    unvme_apoll()    -  Poll an asynchronous read/write for completion.

    unvme_apoll_cs() -  Poll an asynchronous read/write for completion with
                        NVMe command specific DW0 status returned.



Note that a user space filesystem, namely UNFS, has also been developed
at Micron to work with the UNVMe driver.  Such available filesystem enables
major applications like MongoDB to work with UNVMe driver.
See https://github.com/MicronSSD/unfs.git for details.

