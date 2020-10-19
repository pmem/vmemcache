# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

include(${SRC_DIR}/helpers.cmake)

setup()

execute(0 ${TEST_DIR}/vmemcache_test_heap_usage ${TEST_POOL_LOCATION})

cleanup()
