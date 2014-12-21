#!/bin/bash

if [[ "$1" = "-h" ]];  then
    echo Usage: $0 /path/to/VIDEO_TS /path/to/unsplit/dir
fi

shopt -s nullglob

mkdir -p $2

# menu
for a in ${1}/VIDEO_TS.*VOB ${1}/VTS_{{1..9}{0..9},0{1..9}}_0.*VOB; do
    cp $a $2
done

# title
for a in ${1}/VTS_{{1..9}{0..9},0{1..9}}_1.*VOB; do
    cat $a ${a/_1.VOB/}*VOB > ${2}/$(basename $a)
done
