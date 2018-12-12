libvmemcache: buffer based LRU cache
=======================================

### WARNING ###

This library is in a '**Work-In-Progress**' state,
**API is not stable** and it **may change at any time**.

# Building The Source #

Requirements:
- cmake >= 3.3
- git

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
