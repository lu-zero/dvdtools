#!/bin/bash

if [[ "$1" = "-h" ]];  then
    echo Usage: $0 /path/to/encoded/vts /path/to/collated/dir
    exit 0
fi

mkdir -p ${2}

time for a in ${1}/*; do
    name=${2}$(basename $a).VOB
    cat ${a}/*.vob > ${name}
done
