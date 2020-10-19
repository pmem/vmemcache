# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

include(${SRC_DIR}/helpers.cmake)

set(TIMEOUT_SEC 2)

setup()

execute(0 ${TEST_DIR}/vmemcache_test_utilization -d ${TEST_POOL_LOCATION}
		-t ${TIMEOUT_SEC} -n)

cleanup()
