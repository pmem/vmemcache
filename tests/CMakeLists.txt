#
# Copyright 2018-2019, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set(DEFAULT_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/test)

set(TEST_DIR ${DEFAULT_TEST_DIR}
	CACHE STRING "directory for vmemcache memory pool used for tests")

set(DEVICE_DAX_PATH "" CACHE STRING
	"raw DAX device without a file system for tests. Example: /dev/dax0.0")

set(GLOBAL_TEST_ARGS -DPARENT_DIR=${TEST_DIR}/)

if(TRACE_TESTS)
	set(GLOBAL_TEST_ARGS ${GLOBAL_TEST_ARGS} --trace-expand)
endif()

add_cstyle(tests)
add_check_whitespace(tests)

set(vg_tracers memcheck helgrind drd)

# Configures test ${name}
function(test name file tracer location)
	if (${tracer} IN_LIST vg_tracers)
		if (NOT VALGRIND_FOUND)
			message(WARNING
				"Valgrind not found, test skipped: ${name}")
			return()
		endif()
		if (COVERAGE_BUILD)
			message(STATUS
				"This is the Coverage build, skipping Valgrind test: ${name}")
			return()
		endif()
	endif()

	if ((COVERAGE_BUILD) AND (${tracer} STREQUAL helgrind))
		message(STATUS "This is the Coverage build, skipping Helgrind test: ${name}")
		return()
	endif()

	if(${location} STREQUAL fs)
		set(TEST_POOL_LOCATION ${TEST_DIR})
	elseif(${location} STREQUAL ddax)
		set(TEST_POOL_LOCATION ${DEVICE_DAX_PATH})
	else()
		message(FATAL_ERROR "Unknown pool's location: ${location}")
		return()
	endif()

	add_test(NAME ${name}
		COMMAND ${CMAKE_COMMAND}
		${GLOBAL_TEST_ARGS}
		-DTEST_NAME=${name}
		-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
		-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${file}
		-DCONFIG=$<CONFIG>
		-DTRACER=${tracer}
		-DTEST_POOL_LOCATION=${TEST_POOL_LOCATION}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/${file}.cmake)

	set_tests_properties(${name} PROPERTIES
		ENVIRONMENT "LC_ALL=C;PATH=$ENV{PATH}"
		TIMEOUT 300)
endfunction()

# add and link an executable
function(add_link_executable name sources libs)
	add_executable(${name} ${sources})
	target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
	target_link_libraries(${name} PRIVATE ${libs})
endfunction()

set(SOURCES_BASIC
	vmemcache_test_basic.c)

set(SOURCES_MT
	vmemcache_test_mt.c
	${CMAKE_SOURCE_DIR}/src/os_posix.c
	${CMAKE_SOURCE_DIR}/src/os_thread_posix.c)

set(LIBS_BASIC
	vmemcache)

set(LIBS_MT
	vmemcache
	${CMAKE_THREAD_LIBS_INIT})

add_link_executable(vmemcache_test_basic
		"${SOURCES_BASIC}"
		"${LIBS_BASIC}")

add_link_executable(vmemcache_test_mt
		"${SOURCES_MT}"
		"${LIBS_MT}")

if(NOT "${TEST_DIR}" STREQUAL "")
	test("FS-test-basic" test-basic none fs)
	test("FS-test-basic-memcheck" test-basic memcheck fs)
	test("FS-test-mt" test-mt none fs)
	test("FS-test-mt-memcheck" test-mt memcheck fs)
	test("FS-test-mt-helgrind" test-mt helgrind fs)
	test("FS-test-mt-drd" test-mt drd fs)
	test("FS-test-bench-mt" test-bench-mt none fs)
endif()

if(NOT "${DEVICE_DAX_PATH}" STREQUAL "")
	test("DDAX-test-basic" test-basic none ddax)
	test("DDAX-test-basic-memcheck" test-basic memcheck ddax)
	test("DDAX-test-mt" test-mt none ddax)
	test("DDAX-test-mt-memcheck" test-mt memcheck ddax)
	test("DDAX-test-mt-helgrind" test-mt helgrind ddax)
	test("DDAX-test-mt-drd" test-mt drd ddax)
	test("DDAX-test-bench-mt" test-bench-mt none ddax)
endif()
