#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#include "common.h"

static void help(char *name)
{
    fprintf(stderr,
            "Repair the NAV Packet sector information\n"
            "%s <vts> <outvts>\n"
            "vts: collated vts file.\n"
            "outvts: outputvts file",
            name);
    exit(0);
}

int wrap_count = 0;

#define WRAP_SIZE (1024 * 1024 * 1024 / 2048)
AVIOContext *out = NULL;

static int write_vob(VOBU *vobu, AVIOContext *in /* , int title, int *part */)
{
    int len = vobu->end_sector - 1 - vobu->start_sector;
    uint8_t buf[2048];
    int ret = 0, size;
    int split = 0;
    int n;
    int64_t pos;


/*
    if ((vobu->end_sector - *part * WRAP_SIZE) > WRAP_SIZE) {
        printf("----->8-----\n    cut here\n-----8<------\n");
        (*part)++;
        split = 1;
    }
*/
    printf("0x%08"PRIx32"\n"
            "0x%08"PRIx32" 0x%08"PRIx32"\n"
           "0x%08"PRIx32" 0x%08"PRIx32"\n"
           "0x%08"PRIx32" 0x%08"PRIx32"\n"
           "next 0x%08"PRIx32"\n"
           " 0x%04"PRIx32" 0x%04"PRIx32"\n",
             vobu->pci.pci_gi.nv_pck_lbn,
             vobu->start_sector,
             vobu->end_sector - 1 - vobu->start_sector,
             vobu->dsi.dsi_gi.nv_pck_lbn,
             vobu->dsi.dsi_gi.vobu_ea,
             vobu->dsi.dsi_gi.nv_pck_lbn - vobu->start_sector,
             vobu->dsi.dsi_gi.vobu_ea - len,
             vobu->dsi.vobu_sri.next_vobu & 0x3fffffff,
             vobu->dsi.dsi_gi.vobu_c_idn,
             vobu->dsi.dsi_gi.vobu_vob_idn);


    avio_seek(in, vobu->start, SEEK_SET);


    // write down the NAV_PACK

    n = avio_read(in, buf, sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "Can't read!\n");
        exit(1);
    }

    pos = avio_tell(out);
    av_log(NULL, AV_LOG_ERROR, "Start Position %"PRId64"\n",
           avio_tell(out));

    avio_write(out, buf, n);

    avio_seek(out, pos, SEEK_SET);

    avio_seek(out, 44 + 1, SEEK_CUR);

    avio_wb32(out, vobu->start_sector);

    avio_seek(out, pos + 1030 + 1 + 4, SEEK_SET);

    avio_wb32(out, vobu->start_sector);
    avio_wb32(out, len);

    avio_seek(out, 302, SEEK_CUR);
    avio_wb32(out, vobu->next);

    av_log(NULL, AV_LOG_ERROR, "Next %"PRIx32"\n",
           vobu->next);

    avio_seek(out, pos + sizeof(buf), SEEK_SET);

    av_log(NULL, AV_LOG_ERROR, "Position %"PRId64"\n",
           avio_tell(out));

    size = vobu->end - vobu->start - sizeof(buf);

    while (size > 0) {
        n = avio_read(in, buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "OMGBBQ\n");
            break;
        }
        avio_write(out, buf, n);
        size -= n;
    }

    avio_flush(out);

    return 0;
}

int main(int argc, char *argv[])
{
    AVIOContext *in = NULL;
    VOBU *vobus = NULL;
    int ret, i = 0, nb_vobus;
    av_register_all();

    if (argc < 2)
        help(argv[0]);

    ret = avio_open(&in, argv[1], AVIO_FLAG_READ);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_ERROR, "Cannot open %s: %s",
               argv[1], errbuf);
        return 1;
    }

    nb_vobus = populate_vobs(&vobus, argv[1]);

    avio_open(&out, argv[2], AVIO_FLAG_WRITE);

    for (i = 0; i < nb_vobus; i++) {
        ret = write_vob(vobus + i, in);
        if (ret < 0) {
            exit(1);
        }
    }

    av_free(vobus);

    avio_close(in);
    avio_close(out);

    return 0;
}
