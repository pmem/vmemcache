# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2018-2019, Intel Corporation

cmake_minimum_required(VERSION 3.3)
project(vmemcache C)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 8)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR})

set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR})

include(FindThreads)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

include(CheckCCompilerFlag)
include(GNUInstallDirs)

if(NOT MSVC)
	find_package(PkgConfig QUIET)
	if(NOT PKG_CONFIG_FOUND)
		message(WARNING "Pkg-config not found. Detection of other dependencies may fail.")
	endif()
endif()

if(NOT WIN32)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(VALGRIND QUIET valgrind)
	else()
		find_package(VALGRIND QUIET)
	endif()

	if (NOT VALGRIND_FOUND)
		message(WARNING "Valgrind not found. Some tests will be skipped.")
	endif()
endif()

find_program(PANDOC NAMES pandoc)
if(NOT PANDOC)
	message(WARNING "pandoc not found - documentation will not be generated")
endif()

set(CMAKE_C_STANDARD 99)

include(CheckSymbolExists)
CHECK_SYMBOL_EXISTS(getentropy unistd.h HAVE_GETENTROPY)

# Checks whether flag is supported by current C compiler and appends
# it to the relevant cmake variable.
# 1st argument is a flag
# 2nd (optional) argument is a build type (debug, release, relwithdebinfo)
macro(add_c_flag flag)
	string(REPLACE - _ flag2 ${flag})
	string(REPLACE " " _ flag2 ${flag2})
	string(REPLACE = "_" flag2 ${flag2})
	set(check_name "C_HAS_${flag2}")

	check_c_compiler_flag("${flag}" "${check_name}")

	if (${${check_name}})
		if (${ARGC} EQUAL 1)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}")
		else()
			set(CMAKE_C_FLAGS_${ARGV1} "${CMAKE_C_FLAGS_${ARGV1}} ${flag}")
		endif()
	endif()
endmacro()

if(NOT MSVC)
	add_c_flag(-fno-common)
	add_c_flag(-Wall)
	add_c_flag(-Wconversion)
	add_c_flag(-Wfloat-equal)
	add_c_flag(-Wmissing-field-initializers)
	add_c_flag(-Wmissing-prototypes)
	add_c_flag(-Wmissing-variable-declarations)
	add_c_flag(-Wpointer-arith)
	add_c_flag(-Wsign-compare)
	add_c_flag(-Wsign-conversion)
	add_c_flag(-Wswitch-default)
	add_c_flag(-Wunused-macros)
	add_c_flag(-Wunreachable-code-return)
	add_c_flag(-Werror=incompatible-pointer-types)

	# Place each function or data item into its own section. Will be used to strip unneeded symbols.
	add_c_flag(-fdata-sections)
	add_c_flag(-ffunction-sections)

	add_c_flag(-ggdb DEBUG)
	add_c_flag(-DDEBUG DEBUG)

	add_c_flag(-ggdb RELWITHDEBINFO)
	add_c_flag(-fno-omit-frame-pointer RELWITHDEBINFO)

	check_c_compiler_flag(-Wl,-z,relro LINKER_HAS_RELRO)
	if(LINKER_HAS_RELRO)
		set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS}  -Wl,-z,relro")
		set(CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS}  -Wl,-z,relro")
	endif()

	check_c_compiler_flag(-Wl,--warn-common LINKER_HAS_WARN_COMMON)
	if(LINKER_HAS_WARN_COMMON)
		set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,--warn-common")
		set(CMAKE_SHARED_LINKER_FLAGS  "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--warn-common")
	endif()

	add_c_flag("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2" RELEASE)
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
	IMMEDIATE @ONLY)

add_custom_target(uninstall
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

function(add_cstyle name)
	if(${ARGC} EQUAL 1)
		add_custom_target(cstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/utils/cstyle
			${CMAKE_CURRENT_SOURCE_DIR}/*.c
			${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(cstyle-${name}
			COMMAND ${PERL_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/utils/cstyle ${ARGN})
	endif()
	add_dependencies(cstyle cstyle-${name})
endfunction()

# Generates check-whitespace-$name target and attaches it as a dependency
# of global "check-whitespace" target. This target verifies C files in current
# source dir do not have any whitespace errors.
# If more arguments are used, then they are used as files to be checked
# instead.
# ${name} must be unique.

function(add_check_whitespace name)
	if(${ARGC} EQUAL 1)
		add_custom_target(check-whitespace-${name}
			COMMAND ${PERL_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/utils/check_whitespace
			${CMAKE_CURRENT_SOURCE_DIR}/*.c
			${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(check-whitespace-${name}
			COMMAND ${PERL_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/utils/check_whitespace ${ARGN})
	endif()
	add_dependencies(check-whitespace check-whitespace-${name})
endfunction()

add_custom_target(checkers ALL)
add_custom_target(cstyle)
add_custom_target(check-whitespace)
add_custom_target(check-license
	COMMAND ${CMAKE_SOURCE_DIR}/utils/check_license/check-headers.sh
	${CMAKE_SOURCE_DIR}
	BSD-3-Clause)

add_check_whitespace(other
	${CMAKE_SOURCE_DIR}/utils/check_license/*.sh
	${CMAKE_SOURCE_DIR}/README.md)

option(STATS_ENABLED "statistics are enabled" ON)

option(DEVELOPER_MODE "enable developer checks" OFF)
if(DEVELOPER_MODE)
	add_c_flag(-Werror)
	add_dependencies(checkers cstyle)
	add_dependencies(checkers check-whitespace)
endif(DEVELOPER_MODE)

option(TRACE_TESTS
 	"more verbose test outputs" OFF)

configure_file(libvmemcache.pc.in libvmemcache.pc)
install(FILES ${CMAKE_BINARY_DIR}/libvmemcache.pc
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)
if (PANDOC)
	add_subdirectory(doc)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/doc/vmemcache.3
		DESTINATION ${CMAKE_INSTALL_MANDIR}/man3)
endif()

if(NOT "${CPACK_GENERATOR}" STREQUAL "")
	include(${CMAKE_SOURCE_DIR}/packages.cmake)
endif()
