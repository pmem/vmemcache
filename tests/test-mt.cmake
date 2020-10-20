# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2018-2019, Intel Corporation

include(${SRC_DIR}/helpers.cmake)

setup()

set(SEED 0) # set seed from time
set(vg_thread_tracers helgrind drd)
set(valgrind memcheck helgrind drd)

if (${TRACER} IN_LIST vg_thread_tracers)
	set(N_THREADS 4)
	set(N_OPS 400)
else()
	set(N_THREADS 10)
	set(N_OPS 10000)
endif()

if (${TRACER} IN_LIST valgrind)
	# skip tests that last very long under Valgrind
	execute(0 ${TEST_DIR}/vmemcache_test_mt ${TEST_POOL_LOCATION} ${N_THREADS} ${N_OPS} ${SEED} "skip")
else()
	execute(0 ${TEST_DIR}/vmemcache_test_mt ${TEST_POOL_LOCATION} ${N_THREADS} ${N_OPS} ${SEED})

	# additional tests for number of threads == 1 and 2
	execute(0 ${TEST_DIR}/vmemcache_test_mt ${TEST_POOL_LOCATION} 1)
	execute(0 ${TEST_DIR}/vmemcache_test_mt ${TEST_POOL_LOCATION} 2)
endif()

cleanup()
