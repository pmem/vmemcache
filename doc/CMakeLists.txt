# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2019, Intel Corporation

add_custom_target(doc ALL
	BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/vmemcache.3
	COMMENT "Building vmemcache.3"
	COMMAND ${CMAKE_SOURCE_DIR}/utils/md2man/md2man.sh
		${CMAKE_CURRENT_SOURCE_DIR}/vmemcache.md
		${CMAKE_SOURCE_DIR}/utils/md2man/default.man
		${CMAKE_CURRENT_BINARY_DIR}/vmemcache.3
	)
