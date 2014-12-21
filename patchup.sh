#!/bin/bash

if [[ "$1" = "-h" ]];  then
    echo Usage: $0 /path/to/encoded/vts /path/to/patched/up/vts
    exit 0
fi

mkdir -p ${2}

for a in ${1}/*VOB; do
    name=$(basename $a)
    ./make_vob ${a} ${2}/${name}
done
