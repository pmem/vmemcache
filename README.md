libvmemcache: buffer based LRU cache
=======================================

[![Build Status](https://travis-ci.org/pmem/vmemcache.svg?branch=master)](https://travis-ci.org/pmem/vmemcache)
[![Coverage Status](https://codecov.io/github/pmem/vmemcache/coverage.svg?branch=master)](https://codecov.io/gh/pmem/vmemcache/branch/master)

## ⚠️ Discontinuation of the project
The **vmemcache** project will no longer be maintained by Intel.
- Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases,
or updates, to this project.
- Intel no longer accepts patches to this project.
- If you have an ongoing need to use this project, are interested in independently developing it, or would like to
maintain patches for the open source software community, please create your own fork of this project.
- You will find more information [here](https://pmem.io/blog/2022/11/update-on-pmdk-and-our-long-term-support-strategy/).

## Introduction

**libvmemcache** is an embeddable and lightweight in-memory caching solution.
It's designed to fully take advantage of large capacity memory, such as
Persistent Memory with DAX, through memory mapping in an efficient
and scalable way.

The things that make it unique are:
- Extent-based memory allocator which sidesteps the fragmentation
problem that affects most in-memory databases and allows the cache
to achieve very high space utilization for most workloads.
- Buffered LRU, which combines a traditional LRU doubly-linked
list with a non-blocking ring buffer to deliver high degree
of scalability on modern multi-core CPUs.
- Unique indexing structure, critnib, which delivers
high-performance while being very space efficient.

The cache is tuned to work optimally with relatively large value sizes. The
smallest possible size is 256 bytes, but libvmemcache works best if the expected
value sizes are above 1 kilobyte.

## Building The Source ##

Requirements:
- cmake >= 3.3

Optional:
- valgrind (for tests)
- pandoc (for documentation)

For all systems:

```sh
$ git clone https://github.com/pmem/vmemcache.git
$ cd vmemcache
$ mkdir build
$ cd build
```

And then:

### On RPM-based Linux distros (Fedora, openSUSE, RHEL, SLES) ###

```sh
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCPACK_GENERATOR=rpm
$ make package
$ sudo rpm -i libvmemcache*.rpm
```

### On DEB-based Linux distros (Debian, Ubuntu) ###

```sh
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCPACK_GENERATOR=deb
$ make package
$ sudo dpkg -i libvmemcache*.deb
```

### On other Linux distros ###
```sh
$ cmake .. -DCMAKE_INSTALL_PREFIX=~/libvmemcache-bin
$ make
$ make install
```

# Statistics #

Statistics are enabled by default. They can be disabled at the compile time
of the libvmemcache library if the **STATS_ENABLED** CMake option is set to OFF.

See the man page for more information about statistics.
