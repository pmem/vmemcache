#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2019, Intel Corporation

#
# run-build.sh - is called inside a Docker container; prepares the environment
#                and starts a build of libvmemcache.
#

set -e

[ "$WORKDIR" != "" ] && cd $WORKDIR
INSTALL_DIR=/tmp/vmemcache

mkdir -p $INSTALL_DIR

# -----------------------------------------
# gcc & Debug

echo
echo " ##########################################"
echo " # Running the configuration: gcc & Debug #"
echo " ##########################################"
echo

mkdir -p build
cd build

CC=gcc \
cmake .. -DCMAKE_BUILD_TYPE=Debug \
	-DDEVELOPER_MODE=1 \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
	-DTRACE_TESTS=1

make -j2
ctest --output-on-failure

make install
make uninstall

cd ..
rm -r build

# -----------------------------------------
# gcc & Release

echo
echo " ############################################"
echo " # Running the configuration: gcc & Release #"
echo " ############################################"
echo

mkdir build
cd build

CC=gcc \
cmake .. -DCMAKE_BUILD_TYPE=Release \
	-DDEVELOPER_MODE=1 \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
	-DTRACE_TESTS=1

make -j2
ctest --output-on-failure

cd ..
rm -r build

# -----------------------------------------
# Clang & Debug

echo
echo " ############################################"
echo " # Running the configuration: Clang & Debug #"
echo " ############################################"
echo

mkdir build
cd build

CC=clang \
cmake .. -DCMAKE_BUILD_TYPE=Debug \
	-DDEVELOPER_MODE=1 \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
	-DTRACE_TESTS=1

make -j2
ctest --output-on-failure

cd ..
rm -r build

# -----------------------------------------
# Clang & Release

echo
echo " ##############################################"
echo " # Running the configuration: Clang & Release #"
echo " ##############################################"
echo

mkdir build
cd build

CC=clang \
cmake .. -DCMAKE_BUILD_TYPE=Release \
	-DDEVELOPER_MODE=1 \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
	-DTRACE_TESTS=1

make -j2
ctest --output-on-failure

cd ..
rm -r build

# -----------------------------------------
# deb & rpm

echo
echo " ##########################################"
echo " # Running the configuration: deb & rpm   #"
echo " ##########################################"
echo

mkdir -p build
cd build

CC=gcc \
cmake .. -DCMAKE_BUILD_TYPE=Release \
	-DCPACK_GENERATOR=$PACKAGE_MANAGER \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DTRACE_TESTS=1

make -j2
ctest --output-on-failure

make package

find . -iname "libvmemcache*.$PACKAGE_MANAGER"

if [ $PACKAGE_MANAGER = "deb" ]; then
	echo "$ dpkg-deb --info ./libvmemcache*.deb"
	dpkg-deb --info ./libvmemcache*.deb

	echo "$ dpkg-deb -c ./libvmemcache*.deb"
	dpkg-deb -c ./libvmemcache*.deb

	echo "$ sudo -S dpkg -i ./libvmemcache*.deb"
	echo $USERPASS | sudo -S dpkg -i ./libvmemcache*.deb

elif [ $PACKAGE_MANAGER = "rpm" ]; then
	echo "$ rpm -q --info ./libvmemcache*.rpm"
	rpm -q --info ./libvmemcache*.rpm && true

	echo "$ rpm -q --list ./libvmemcache*.rpm"
	rpm -q --list ./libvmemcache*.rpm && true

	echo "$ sudo -S rpm -ivh --force *.rpm"
	echo $USERPASS | sudo -S rpm -ivh --force *.rpm
fi

cd ..
rm -rf build

# -----------------------------------------
# Coverage
if [[ $COVERAGE -eq 1 ]] ; then
	echo
	echo " #######################################"
	echo " # Running the configuration: Coverage #"
	echo " #######################################"
	echo

	mkdir build
	cd build

	CC=gcc \
	cmake .. -DCMAKE_BUILD_TYPE=Debug \
		-DTRACE_TESTS=1 \
		-DCOVERAGE_BUILD=1 \
		-DCMAKE_C_FLAGS=-coverage

	make -j2
	ctest --output-on-failure
	bash <(curl -s https://codecov.io/bash) -c

	cd ..

	rm -r build
fi
