#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2017-2018, Intel Corporation

#
# build-local.sh - runs a Docker container from a Docker image with environment
#                  prepared for running tests.
#
#
# Notes:
# - run this script from its location or set the variable 'HOST_WORKDIR' to
#   where the root of this project is on the host machine,
# - set variables 'OS' and 'OS_VER' properly to a system you want to build this
#	repo on (for proper values take a look on the list of Dockerfiles at the
#   utils/docker/images directory), eg. OS=ubuntu, OS_VER=16.04.
#

set -e

if [[ -z "$OS" || -z "$OS_VER" ]]; then
	echo "ERROR: The variables OS and OS_VER have to be set " \
		"(eg. OS=ubuntu, OS_VER=16.04)."
	exit 1
fi

if [[ -z "$HOST_WORKDIR" ]]; then
	HOST_WORKDIR=$(readlink -f ../..)
fi

chmod -R a+w $HOST_WORKDIR

imageName=${DOCKERHUB_REPO}:${OS}-${OS_VER}
containerName=vmemcache-${OS}-${OS_VER}

if [[ "$command" == "" ]]; then
	command="./run-build.sh";
fi

if [ -n "$DNS_SERVER" ]; then DNS_SETTING=" --dns=$DNS_SERVER "; fi

WORKDIR=/vmemcache
SCRIPTSDIR=$WORKDIR/utils/docker

echo Building ${OS}-${OS_VER}

ci_env=`bash <(curl -s https://codecov.io/env)`
# Run a container with
#  - environment variables set (--env)
#  - host directory containing source mounted (-v)
#  - working directory set (-w)
docker run --privileged=true --name=$containerName -ti \
	$DNS_SETTING \
	${docker_opts} \
	$ci_env \
	--env http_proxy=$http_proxy \
	--env https_proxy=$https_proxy \
	--env GITHUB_TOKEN=$GITHUB_TOKEN \
	--env WORKDIR=$WORKDIR \
	--env SCRIPTSDIR=$SCRIPTSDIR \
	--env COVERAGE=$COVERAGE \
	--env TRAVIS_REPO_SLUG=$TRAVIS_REPO_SLUG \
	--env TRAVIS_BRANCH=$TRAVIS_BRANCH \
	--env TRAVIS_EVENT_TYPE=$TRAVIS_EVENT_TYPE \
	-v $HOST_WORKDIR:$WORKDIR \
	-v /etc/localtime:/etc/localtime \
	-w $SCRIPTSDIR \
	$imageName $command
