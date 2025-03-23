#!/bin/sh

readonly arsdir="ars"
readonly incdir="include"

if [[ "$0" != './build.sh' ]]; then
	echo "You must run this script in the libs directory" >&2
	exit 1
fi

mkdir -p "$arsdir"
mkdir -p "$incdir"

for d in $(find . -type d -maxdepth 1 -mindepth 1 | cut -c 3-); do
	if [[ ! "$d" =~ ^lib.*$ ]]; then
		continue
	fi
	cd "$d"
	cp *.h "../$incdir"
	make
	cp *.a "../$arsdir"
	cd ..
done
