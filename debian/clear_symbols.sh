#!/bin/bash -x

TO_STRIP="GLIBC_2.34"
LIB="${1}"

for version in ${TO_STRIP}; do
	args=""
	for symbol in $(nm --dynamic --with-symbol-versions "${LIB}"  | grep ${version} | awk '{ print $2 }' | cut -d@ -f1); do
		args="${args} --clear-symbol-version ${symbol}"
	done
	patchelf ${args} ${LIB}
done
