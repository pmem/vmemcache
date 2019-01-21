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

set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/test
	CACHE STRING "working directory for tests")

set(DEVICE_DAX_PATHS "" CACHE STRING
	"for tests that require raw dax devices without a file system. Some tests might require two DAX devices.
	Example: '/dev/dax1.0 /dev/dax2.0'")

set(GLOBAL_TEST_ARGS
	-DPARENT_DIR=${TEST_DIR}/
	-DDEVICE_DAX_PATHS=${DEVICE_DAX_PATHS})

# convert the DEVICE_DAX_PATHS list to the array
if(DEVICE_DAX_PATHS)
	string(REPLACE " " ";" DEVICE_DAX_PATHS ${DEVICE_DAX_PATHS})
endif()

list(LENGTH DEVICE_DAX_PATHS devdax_num)

if(TRACE_TESTS)
	set(GLOBAL_TEST_ARGS ${GLOBAL_TEST_ARGS} --trace-expand)
endif()

add_cstyle(tests ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_check_whitespace(tests ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

set(vg_tracers memcheck helgrind)

# Configures test ${name}
function(test name file tracer)
	if ((${tracer} IN_LIST vg_tracers) AND (NOT VALGRIND_FOUND))
		message(WARNING "Valgrind not found, test skipped: ${name}")
		return()
	endif()

	if ((COVERAGE_BUILD) AND (${tracer} STREQUAL helgrind))
		message(STATUS "This is the Coverage build, skipping Helgrind test: ${name}")
		return()
	endif()

	if (${tracer} STREQUAL helgrind)
		set(N_THREADS 2)
		set(N_OPS 200)
	else()
		set(N_THREADS 10)
		set(N_OPS 10000)
	endif()

	add_test(NAME ${name}
		COMMAND ${CMAKE_COMMAND}
		${GLOBAL_TEST_ARGS}
		-DTEST_NAME=${name}
		-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
		-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${file}
		-DCONFIG=$<CONFIG>
		-DTRACER=${tracer}
		-DN_THREADS=${N_THREADS}
		-DN_OPS=${N_OPS}
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

test("test-basic" test-basic none)
test("test-basic-memcheck" test-basic memcheck)
test("test-mt" test-mt none)
test("test-mt-memcheck" test-mt memcheck)
test("test-mt-helgrind" test-mt helgrind)
test("test-bench-mt" test-bench-mt none)
