for a in /mnt/vbox/nouveautés/*; do
    mount -o loop $a /mnt/loop
    for b in /mnt/loop/VIDEO_TS/VIDEO.TS /mnt/loop/VIDEO_TS/VTS_*_0.VOB; do
        if ./print_vobu $b 2>&1 | grep Empty; then
            echo $a $b >> /tmp/empty-nav.log
            echo $a $b Empty
        else
            echo $a $b Ok
        fi
    done
    umount /mnt/loop
done
