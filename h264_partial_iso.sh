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

if [[ "$#" -lt 2 ]]; then
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

wipe="$3"

ISOFILE="$1"
DESTFILE="$2"
WORKDIR="/mnt/work/.$(basename ${ISOFILE})_partial"
ENCRYPTED="${WORKDIR}/encrypted"
DVDCSS_CACHE="${WORKDIR}/dvdccs-cache"
MOUNTPOINT="${WORKDIR}/loop"
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

if [[ "$wipe" = "wipe" ]]; then
    rm -fR ${WORKDIR}
fi

do_unpack(){
    echo Unpacking the iso...
    export DVDCSS_CACHE="${DVDCSS_CACHE}"
    mkdir -p ${ORIGIN}
    iso2vob -a http://intranet.prod.vodkaster.com/dvd2iso/authent/ -i ${ISOFILE} -o ${ORIGIN} -x ${XML_DESC} -s ${ENCRYPTED} -e || \
        die "iso2vob -i ${ISOFILE} -o ${ORIGIN} -x ${XML_DESC} -e"
    echo iso2vob succeeded

    ## FIXME workaround
    mkdir -p ${MOUNTPOINT}
    mount ${ISOFILE} ${MOUNTPOINT}
    cp "${MOUNTPOINT}/VIDEO_TS/"*.IFO "${ORIGIN}/VIDEO_TS"
    umount ${MOUNTPOINT}
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
AVCONV_ENC="-v error -vsync passthrough "
AVCONV_ENC+="-c:v libx264 -preset slow -tune film "
AVCONV_ENC+="-c:a copy -c:s copy -map 0 -f dvd -y"

do_encode(){
    echo Encoding...
    mkdir -p ${ENC_SPLIT}

    for a in ${SPLIT}/*_1; do
        dir=${ENC_SPLIT}$(basename $a)
        mkdir -p $dir
        for b in ${a}/*_d.vob; do
            name=$(basename ${b})
            echo encoding $b
            ${AVCONV} -i $b ${AVCONV_ENC} ${dir}/${name} || die "avconv ${AVCONV} -i $b ${AVCONV_ENC} ${dir}/${name}"


#            2> ${ENC_SPLIT}/log;
# then
#                echo Encoding OK
#            else
#                if grep "Could not find codec parameters (Video:" ${ENC_SPLIT}/log; then
#                    echo "COPY Unknown Video ${name}"
#                    cp ${b} ${dir}
#                else
#                    die "Encoding ${AVCONV} -i $b ${AVCONV_ENC} ${dir}/${name}"
#                fi
        done
#        rm -f ${ENC_SPLIT}/log
        cp ${a}/*_e.vob ${dir}
    done
}

do_unify(){
    echo Building unified vob files...
    mkdir -p ${EU}

    time for a in ${ENC_SPLIT}/*; do
        name=${EU}/$(basename $a).VOB
        echo "Processing $name"
        if [[ -z "$(echo ${a}/*.vob)" ]]; then
            touch ${name}
        else
            cat ${a}/*.vob > ${name}
        fi
    done
}

do_patch_nav(){
    echo Patching nav packets...
    ## FIXME make it a single pass
    mkdir -p ${PD}

    for a in ${EU}/*.VOB; do
        name=$(basename $a)
        echo Processing $name
        make_vob $a ${PD}/${name}.t || die "makevob ${name}.t"
        make_vob ${PD}/${name}.t ${PD}/${name} || die "makevob $name"
    done

    echo Copying the menus
    cp "${UNSPLIT}"/*_0.VOB  "${UNSPLIT}"/*VIDEO_TS.VOB ${PD}
}


do_patch_ifo(){
    echo Patching ifo files...
    mkdir -p ${OUTDIR}

    echo rewrite_ifo ${ORIGIN} ${PATCHED}

    ## The VIDEO_TS.IFO must be processed once one to account for the
    ## the possible change in the IFO sizes.
    for a in ${OR}/*.IFO ${OR}/VIDEO_TS.IFO; do
        ifo=$(basename $a)
        idx=$(basename $a | sed -e "s:VTS_\([[:digit:]]*\)_0.IFO:\1:")
        echo Processing $ifo
        rewrite_ifo ${ORIGIN} ${PATCHED} ${idx} || die "rewrite_ifo $ifo ${ORIGIN} ${PATCHED}"
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
    mkisofs -dvd-video -o ${DESTFILE} ${OUTDIR} || die "mkisofs -dvd-video -o ${DESTFILE} ${OUTDIR}"
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

if [[ "$wipe" = "wipe" ]]; then
    rm -Rf ${WORKDIR}
fi
