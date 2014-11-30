#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#include "common.h"

static void help(char *name)
{
    fprintf(stderr, "%s <vob>\n"
            "vob: A VOB file.\n",
            name);
    exit(0);
}



static int write_vob(VOBU *vobu)
{
    int len = vobu->end_sector - 1 - vobu->start_sector;
    printf("0x%08"PRIx32" 0x%08"PRIx32"\n"
           "0x%08"PRIx32" 0x%08"PRIx32"\n"
           "0x%08"PRIx32" 0x%08"PRIx32"\n"
           "next 0x%08"PRIx32"\n"
           " 0x%04"PRIx32" 0x%04"PRIx32"\n",
             vobu->start_sector,
             vobu->end_sector - 1 - vobu->start_sector,
             vobu->dsi.dsi_gi.nv_pck_lbn,
             vobu->dsi.dsi_gi.vobu_ea,
             vobu->dsi.dsi_gi.nv_pck_lbn - vobu->start_sector,
             vobu->dsi.dsi_gi.vobu_ea - len,
             vobu->dsi.vobu_sri.next_vobu & 0x3fffffff,
             vobu->dsi.dsi_gi.vobu_c_idn,
             vobu->dsi.dsi_gi.vobu_vob_idn);

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

    for (i = 0; i < nb_vobus; i++) {
        ret = write_vob(vobus + i);
        if (ret < 0) {
            exit(1);
        }
    }

    av_free(vobus);

    avio_close(in);

    return 0;
}
