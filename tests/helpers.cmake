#
# Copyright 2017-2018, Intel Corporation
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

cmake_minimum_required(VERSION 3.3)
set(DIR ${PARENT_DIR}/😘⠝⠧⠍⠇ɗVMEMCACHEӜ⥺🙋${TEST_NAME})

if (WIN32)
	set(EXE_DIR ${CMAKE_CURRENT_BINARY_DIR}/../${CONFIG})
	set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/../tests/${CONFIG})
else()
	set(EXE_DIR ${CMAKE_CURRENT_BINARY_DIR}/../)
	set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/../tests/)
endif()

function(setup)
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIR})
	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR})
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${BIN_DIR})
	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${BIN_DIR})
endfunction()

function(cleanup)
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIR})
endfunction()

# Executes test command ${name} and verifies its status matches ${expectation}.
# Optional function arguments are passed as consecutive arguments to
# the command.
function(execute_arg input expectation name)
	message(STATUS "Executing: ${name} ${ARGN}")
	if("${input}" STREQUAL "")
		execute_process(COMMAND ${name} ${ARGN}
			RESULT_VARIABLE RET
			OUTPUT_FILE ${BIN_DIR}/out
			ERROR_FILE ${BIN_DIR}/err)
	else()
		execute_process(COMMAND ${name} ${ARGN}
			RESULT_VARIABLE RET
			INPUT_FILE ${input}
			OUTPUT_FILE ${BIN_DIR}/out
			ERROR_FILE ${BIN_DIR}/err)
	endif()
	message(STATUS "Test ${name}:")
	file(READ ${BIN_DIR}/out OUT)
	message(STATUS "Stdout:\n${OUT}")
	file(READ ${BIN_DIR}/err ERR)
	message(STATUS "Stderr:\n${ERR}")

	if(NOT RET EQUAL expectation)
		message(FATAL_ERROR "${name} ${ARGN} exit code ${RET} doesn't match expectation ${expectation}")
	endif()
endfunction()

function(run_under_memcheck name)
	message(STATUS "Executing: ${name} ${ARGN}")
	execute_process(COMMAND valgrind --leak-check=full ${name} ${ARGN}
			RESULT_VARIABLE RET
			OUTPUT_FILE ${BIN_DIR}/out
			ERROR_FILE ${BIN_DIR}/err)
	file(READ ${BIN_DIR}/err ERR)
	if(NOT RET EQUAL 0)
		message(FATAL_ERROR
			"command 'valgrind ${name} ${ARGN}' failed:\n${ERR}")
	endif()

	set(TEXT_OK "All heap blocks were freed -- no leaks are possible")
	string(FIND "${ERR}" "${TEXT_OK}" RET)
	if(RET EQUAL -1)
		message(FATAL_ERROR
			"command 'valgrind ${name} ${ARGN}' failed:\n${ERR}")
	endif()
endfunction()

function(execute expectation name)
	if (${TRACER} STREQUAL "none")
		execute_arg("" ${expectation} ${name} ${ARGN})
	elseif (${TRACER} STREQUAL memcheck)
		run_under_memcheck(${name} ${ARGN})
	else ()
		message(FATAL_ERROR "unknown tracer: ${TRACER}")
	endif ()
endfunction()
