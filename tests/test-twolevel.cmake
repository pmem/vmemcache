# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

include(${SRC_DIR}/helpers.cmake)

setup()

execute(0 ${TEST_DIR}/twolevel ${TEST_POOL_LOCATION} ${TEST_POOL_LOCATION})

cleanup()
