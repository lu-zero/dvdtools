#!/bin/bash

shopt -s nullglob

die(){
    echo $1 Failed
    exit 1
}

usage(){
    echo "$0 <file.iso> <file-h264.iso>"
    echo "Convert the iso to h264"
}

if [[ "$#" -ne 2 ]]; then
    usage
    exit 1
fi

if [[ "$1" = "-h" ]]; then
    usage
    exit 0
fi

if [[ ! -f "$1" ]]; then
    echo "The file $1 does not exist"
    exit 1
fi


ISOFILE="$1"
DESTFILE="$2"
WORKDIR="/mnt/work/.$(basename ${ISOFILE})"
ORIGIN="${WORKDIR}/origin/"
OR="${ORIGIN}/VIDEO_TS/"
UNSPLIT="${WORKDIR}/unsplit/VIDEO_TS/"
SPLIT="${WORKDIR}/split/VIDEO_TS/"
ENC_SPLIT="${WORKDIR}/encoded_split/"
ENC_UNSPLIT="${WORKDIR}/encoded_unsplit/"
EU="${ENC_UNSPLIT}/VIDEO_TS/"
XML_DESC="${WORKDIR}/desc.xml"
PATCHED="${WORKDIR}/patched/"
PD="${PATCHED}/VIDEO_TS/"
OUTDIR="${WORKDIR}/out"
FN="${OUTDIR}/VIDEO_TS/"

echo "Work directory ${WORKDIR}"

do_unpack(){
    echo Unpacking the iso...
    mkdir -p ${ORIGIN}
    iso2vob -i ${ISOFILE} -o ${ORIGIN} -x ${XML_DESC} -e || die "iso2vob"
    echo iso2vob succeeded
}

do_unsplit(){
    echo Unsplitting VOBs
    mkdir -p ${UNSPLIT}
    # menu
    for a in ${OR}/VIDEO_TS.*VOB ${OR}/VTS_{{1..9}{0..9},0{1..9}}_0.*VOB; do
        cp $a ${UNSPLIT}
    done
    # title
    for a in ${OR}/VTS_{{1..9}{0..9},0{1..9}}_1.*VOB; do
        cat ${a/_1.VOB/}_{1..9}*VOB > ${UNSPLIT}/$(basename $a)
    done
}

do_split(){
    echo Splitting in vob units
    mkdir -p ${SPLIT}

    for a in ${UNSPLIT}*.VOB; do
        name=$(basename ${a/.VOB//})
        outdir=${SPLIT}/${name}
        mkdir -p ${outdir}
        echo Processing $name
        dump_vobu ${a} ${outdir}
    done
}

AVCONV="avconv"
AVCONV_ENC="-vsync passthrough "
AVCONV_ENC+="-c:v libx264 -g 1 -preset superfast "
AVCONV_ENC+="-c:a copy -c:s copy -map 0 -f dvd -y"

do_encode(){
    echo Encoding...
    mkdir -p ${ENC_SPLIT}

    for a in ${SPLIT}/*; do
        dir=${ENC_SPLIT}$(basename $a)
        for b in ${a}/*_d.vob; do
            mkdir -p $dir
            name=$(basename $b)
            ${AVCONV} -i $b ${AVCONV_ENC} ${dir}/${name} || die "Encoding"
        done
    done
}


## FIXME take in account 0-sized nav later
do_unify(){
    echo Building unified vob files...
    mkdir -p ${EU}

    time for a in ${ENC_SPLIT}/*; do
        name=${EU}/$(basename $a).VOB
        echo "Processing $name"
        cat ${a}/*.vob > ${name}
    done
}

do_patch_ifo(){
    echo Patching ifo files...
    mkdir -p ${OUTDIR}

    rewrite_ifo ${ORIGIN} ${PATCHED} 0

    ## Patching the rest
    for a in ${OR}/*.IFO; do
        ifo=$(basename $a)
        idx=$(basename $a | sed -e "s:VTS_\([[:digit:]]*\)_0.IFO:\1:")
        echo Processing $ifo
        rewrite_ifo ${ORIGIN} ${PATCHED} ${idx}
    done
}

do_patch_nav(){
    echo Patching nav packets...
    ## FIXME make it a single pass
    mkdir -p ${PD}

    for a in ${EU}/*.VOB; do
        name=$(basename $a)
        echo Processing $name
        make_vob $a ${PD}/${name}
    done
}

do_finalize(){
    echo Finalizing...
    mkdir -p ${FN}
    split_opts="-a 1 --numeric-suffixes=1 -b 1073575936 --additional-suffix=.VOB"
    for a in ${PD}/*IFO; do
        name=$(basename ${a/.IFO/})
        cp $a ${FN}
        cp $a ${FN}/${name}.BUP
    done
    for a in ${PD}/VTS*_1.VOB; do
        name=$(basename $a)
        split ${split_opts} $a ${FN}/${name/_1.VOB/_}
    done
    for a in ${PD}/VTS*_0.VOB ${PD}/VIDEO_TS.VOB; do
        cp $a ${FN}
    done
}

do_make_iso(){
    echo Packing to iso...
    mkisofs -dvd-video -o ${DESTFILE} ${OUTDIR} || die "mkisofs"
}


do_unpack
do_unsplit
do_split
do_encode
do_unify
do_patch_nav
do_patch_ifo
do_finalize
do_make_iso

