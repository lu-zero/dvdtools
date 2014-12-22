time for a in /mnt/vbox/*/*; do
    mkdir /mnt/work/t -p
    echo "Processing $a"
    iso2vob -i $a -o /mnt/work/t/
    empty=No
    for b in /mnt/work/t/VIDEO_TS/VIDEO.TS /mnt/work/t/VIDEO_TS/VTS_*_0.VOB; do
        if ./print_vobu $b 2>&1 | grep Empty; then
            empty=Yes
        fi
    done

    echo $a >> /tmp/em-${empty}.log
    rm -fR /mnt/work/t/*
done
