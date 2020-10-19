# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2018-2019, Intel Corporation

add_cstyle(benchmarks
	${CMAKE_SOURCE_DIR}/benchmarks/*.c)

add_check_whitespace(benchmarks
	${CMAKE_SOURCE_DIR}/benchmarks/*.c)

set(SOURCES
	benchmark_time.c
	rand.c
	${CMAKE_SOURCE_DIR}/src/os_posix.c
	${CMAKE_SOURCE_DIR}/src/os_thread_posix.c)

add_executable(bench_micro bench_micro.c ${SOURCES})

target_include_directories(bench_micro PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/tests)

target_link_libraries(bench_micro PRIVATE vmemcache)
target_link_libraries(bench_micro PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(bench_simul bench_simul.c ${SOURCES})

target_include_directories(bench_simul PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/tests)

if (HAVE_GETENTROPY)
	target_compile_definitions(bench_simul PRIVATE HAVE_GETENTROPY)
endif()

if(STATS_ENABLED)
	target_compile_definitions(bench_simul PRIVATE STATS_ENABLED=1)
endif()

target_link_libraries(bench_simul PRIVATE vmemcache)
target_link_libraries(bench_simul PRIVATE ${CMAKE_THREAD_LIBS_INIT})
