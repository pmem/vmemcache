#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2019, Intel Corporation
#

#
# md2man.sh -- convert markdown to groff man pages
#
# usage: md2man.sh file template outfile
#
# This script converts markdown file into groff man page using pandoc.
# It performs some pre- and post-processing for better results:
# - parse input file for YAML metadata block and read man page title,
#   section and version
# - cut-off metadata block and license
# - unindent code blocks
#

set -e
set -o pipefail

filename=$1
template=$2
outfile=$3
title=`sed -n 's/^title:\ _MP(*\([A-Za-z_-]*\).*$/\1/p' $filename`
section=`sed -n 's/^title:.*\([0-9]\))$/\1/p' $filename`
version=`sed -n 's/^date:\ *\(.*\)$/\1/p' $filename`

dt=$(date +"%F")
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
YEAR=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y 2>/dev/null ||
	date -u -r "$SOURCE_DATE_EPOCH" +%Y 2>/dev/null || date -u +%Y)
dt=$(date -u -d "@$SOURCE_DATE_EPOCH" +%F 2>/dev/null ||
	date -u -r "$SOURCE_DATE_EPOCH" +%F 2>/dev/null || date -u +%F)
cat $filename | sed -n -e '/# NAME #/,$p' |\
	pandoc -s -t man -o $outfile.tmp --template=$template \
	-V title=$title -V section=$section \
	-V date="$dt" -V version="$version" \
	-V year="$YEAR" |
sed '/^\.IP/{
N
/\n\.nf/{
	s/IP/PP/
    }
}'

# don't overwrite the output file if the only thing that changed
# is modification date (diff output has exactly 4 lines in this case)
if [ -e $outfile ]
then
	difflines=`diff $outfile $outfile.tmp | wc -l || true >2`
	onlydates=`diff $outfile $outfile.tmp | grep "$dt" | wc -l || true`
	if [ $difflines -eq 4 -a $onlydates -eq 1 ]; then
		rm $outfile.tmp
	else
		mv $outfile.tmp $outfile
	fi
else
	mv $outfile.tmp $outfile
fi
