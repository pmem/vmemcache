libvmemcache: buffer based LRU cache
=======================================

# Building The Source #

Requirements:
- libpmem-dev(el) >= 1.3 (http://pmem.io/pmdk/)
- libpmemobj-dev(el) >= 1.3 (http://pmem.io/pmdk/)
- cmake >= 3.3
- git

For all systems:

```sh
$ git clone https://github.com/lplewa/vmemcache.git
$ cd vmemcache
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_INSTALL_PREFIX=/home/user/vmemchache-bin
$ make
$ make install
```
