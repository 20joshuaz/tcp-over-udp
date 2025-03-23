#!/bin/sh

readonly curdir="$(pwd)"
readonly libdir="libs"
readonly reqlibs="$@"

mkdir -p "$libdir"

cd "../../$libdir"

include_lib () {
	for r in $reqlibs; do
		if [[ $(echo "$r" | cut -c 3-) == $(echo "$1" | cut -c 4-)  ]]; then
			return 0
		fi
	done
	return 1
}

for d in $(find . -type d -maxdepth 1 -mindepth 1 | cut -c 3-); do
	if [[ ! "$d" =~ ^lib.*$ ]]; then
		continue
	fi
	if include_lib "$d"; then
		cp -r "$d" "$curdir/$libdir"
		cd "$curdir/$libdir/$d"
		make clean
		cd - > /dev/null
	fi
done

cp build.sh "$curdir/$libdir"
