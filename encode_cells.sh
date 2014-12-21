#!/bin/bash

AVCONV="/usr/src/libav/.dvdh264/avconv"
AVCONV_ENC="-vsync passthrough -c:v libx264 -g 1 -c:a copy -c:s copy -map 0 -f dvd -y"

if [[ "$1" = "-h" ]];  then
    echo Usage: $0 /path/to/cell_split/vts /path/to/encoded/vts
    exit 0
fi

time for a in ${1}/*; do
    dir=${2}$(basename $a)
    mkdir -p $dir
    for b in ${a}/*_d.vob; do
        name=$(basename $b)
        ${AVCONV} -i $b ${AVCONV_ENC} ${dir}/${name}
    done
    for b in ${a}/*_e.vob; do
        name=$(basename $b)
        cp $b ${dir}/${name}
    done
done
