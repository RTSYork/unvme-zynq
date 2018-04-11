#!/bin/python
# Example to write-read-verify synchronously and asynchronously.
#

from __future__ import print_function
import os, sys, argparse, random, time, ctypes, unvme
from ctypes import *

# Write-Read-Verify example test class.
class WRTest:
    def __init__(self, pci, ioc, maxnlb):
        self.ioc = ioc
        self.maxnlb = maxnlb
        self.unvme = unvme.Unvme(pci)
        self.seed = time.time()
        self.ns = self.unvme.info()
        self.wpb = self.ns.blocksize / 8
        self.wbuf = []
        self.rbuf = []
        for i in range(ioc):
            buf = cast(self.unvme.alloc(maxnlb * self.ns.blocksize), POINTER(c_uint64))
            self.wbuf.append(buf)
            buf = cast(self.unvme.alloc(maxnlb * self.ns.blocksize), POINTER(c_uint64))
            self.rbuf.append(buf)

    def __del__(self):
        for i in range(0, self.ioc):
            self.unvme.free(self.wbuf[i])
            self.unvme.free(self.rbuf[i])

    def error(*fmt, **args):
        print(*fmt, file=sys.stderr, **args)
        sys.exit(1)

    def fillBuffer(self, buf, lba, nlb):
        i = 0
        for b in range(nlb):
            pat = (lba << 24) | b
            for w in range(self.wpb):
                buf[i] = pat
                i = i + 1
            lba = lba + 1

    def verifyBuffer(self, buf, lba, nlb):
        i = 0
        for b in range(nlb):
            pat = (lba << 24) | b
            for w in range(self.wpb):
                if buf[i] != pat:
                    self.error('ERROR: data miscompare at lba %#lx' % lba)
                i = i + 1
            lba = lba + 1

    def pickqbc(self, seed):
        random.seed(self.seed + seed)
        nlb = random.randint(1, self.maxnlb)
        lba = random.randrange(0, self.ns.blockcount - nlb - 1)
        q = (lba + nlb) % self.ns.qcount
        return q, lba, nlb

    def syncTest(self):
        print('Sync test');
        for i in range(self.ioc):
            q, lba, nlb = self.pickqbc(i)
            print('  write   q=%-3d lba=0x%-9lx nlb=%#x' % (q, lba, nlb))
            self.fillBuffer(self.wbuf[i], lba, nlb)
            self.unvme.write(q, self.wbuf[i], lba, nlb)

        for i in range(self.ioc):
            q, lba, nlb = self.pickqbc(i)
            print('  read    q=%-3d lba=0x%-9lx nlb=%#x' % (q, lba, nlb))
            self.unvme.read(q, self.rbuf[i], lba, nlb)
            self.verifyBuffer(self.rbuf[i], lba, nlb)

    def asyncTest(self):
        print('Async test');
        wiod = []
        for i in range(self.ioc):
            q, lba, nlb = self.pickqbc(i)
            print('  awrite  q=%-3d lba=0x%-9lx nlb=%#x' % (q, lba, nlb))
            self.fillBuffer(self.wbuf[i], lba, nlb)
            iod = self.unvme.awrite(q, self.wbuf[i], lba, nlb)
            wiod.append(iod)

        riod = []
        for i in range(self.ioc):
            q, lba, nlb = self.pickqbc(i)
            print('  aread   q=%-3d lba=0x%-9lx nlb=%#x' % (q, lba, nlb))
            iod = self.unvme.aread(q, self.rbuf[i], lba, nlb)
            riod.append(iod)

        for i in range(self.ioc):
            q, lba, nlb = self.pickqbc(i)
            print('  apoll   q=%-3d lba=0x%-9lx nlb=%#x' % (q, lba, nlb))
            if self.unvme.apoll(wiod[i], 30) or self.unvme.apoll(riod[i], 30):
                self.error('ERROR: apoll')
            self.verifyBuffer(self.rbuf[i], lba, nlb)


# Main program.
parser = argparse.ArgumentParser(description='RANDOM WRITE-READ TEST EXAMPLE', add_help=False)
parser.add_argument('--ioc', help='number of IOs per test', type=int, default=8)
parser.add_argument('--nlb', help='max number of blocks per IO', type=int, default=8192)
parser.add_argument('pci', help='PCI device name (as 0a:00.0[/1] format)')
if len(sys.argv) == 1:
    parser.print_help()
    sys.exit(1)
opts = parser.parse_args()

print('PYTHON RANDOM WRITE-READ TEST EXAMPLE BEGIN')
ex = WRTest(opts.pci, opts.ioc, opts.nlb)
ex.syncTest()
ex.asyncTest()
print('PYTHON RANDOM WRITE-READ TEST EXAMPLE COMPLETE')

