# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2018-2019, Intel Corporation

cmake_minimum_required(VERSION 3.3)
project(vmemcache C)

add_cstyle(src)
add_check_whitespace(src)

set(SOURCES
	out.c
	os_posix.c
	os_thread_posix.c
	util.c
	util_posix.c
	file.c
	file_posix.c
	fast-hash.c
	mmap.c
	mmap_posix.c
	libvmemcache.c
	critnib.c
	ringbuf.c
	vmemcache.c
	vmemcache_heap.c
	vmemcache_index.c
	vmemcache_repl.c)

add_library(vmemcache SHARED ${SOURCES})
target_link_libraries(vmemcache PRIVATE
	${CMAKE_THREAD_LIBS_INIT}
	-Wl,--version-script=${CMAKE_SOURCE_DIR}/src/libvmemcache.map)
set_target_properties(vmemcache PROPERTIES SOVERSION 0)

target_compile_definitions(vmemcache PRIVATE SRCVERSION="${VERSION}")

if(STATS_ENABLED)
	target_compile_definitions(vmemcache PRIVATE STATS_ENABLED=1)
endif()

if(VALGRIND_FOUND)
	target_compile_definitions(vmemcache PRIVATE VALGRIND_ENABLED=1)
endif()

install(TARGETS vmemcache
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/)
install(FILES libvmemcache.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
