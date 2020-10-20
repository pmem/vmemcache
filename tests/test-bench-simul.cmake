# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

include(${SRC_DIR}/helpers.cmake)

setup()

execute(0 ${TEST_DIR}/../benchmarks/bench_simul "${TEST_POOL_LOCATION}" n_threads=4 ops_count=100 warm_up=0)

cleanup()
