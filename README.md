libvmemcache: buffer based LRU cache
=======================================

[![Build Status](https://travis-ci.org/pmem/vmemcache.svg?branch=master)](https://travis-ci.org/pmem/vmemcache)
[![Coverage Status](https://codecov.io/github/pmem/vmemcache/coverage.svg?branch=master)](https://codecov.io/gh/pmem/vmemcache/branch/master)

### WARNING ###

This library is in a '**Work-In-Progress**' state,
**API is not stable** and it **may change at any time**.

# Building The Source #

Requirements:
- cmake >= 3.3
- git

Optional:
- valgrind (for tests)
- pandoc (for documentation)

For all systems:

```sh
$ git clone https://github.com/pmem/vmemcache.git
$ cd vmemcache
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_INSTALL_PREFIX=~/vmemcache-bin
$ make
$ make install
```

# Statistics #

Statistics are enabled by default. They can be disabled at the compile time
of the libvmemcache library if the **STATS_ENABLED** CMake option is set to OFF.

See the man page for more information about statistics.
