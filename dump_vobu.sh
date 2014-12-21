#!/bin/bash

if [[ "$1" = "-h" ]];  then
    echo Usage: $0 /path/to/unsplit/vts /path/to/split/dir
    exit 0
fi

time for a in ${1}*.VOB; do
    name=$(basename ${a/.VOB//})
    outdir=${2}/${name}
    mkdir -p ${outdir}
    echo Processing $name
    ./dump_vobu ${a} ${outdir} &> ${outdir}/dump.log
done

