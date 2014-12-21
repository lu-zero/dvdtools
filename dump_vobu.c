#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#include "common.h"

static void help(char *name)
{
    fprintf(stderr, "%s <vob> <outpath>\n"
            "vob: A VOB file.\n"
            "outpath: output path.\n",
            name);
    exit(0);
}

AVIOContext *out = NULL;
int cell_idn = -1;
static int write_vob(VOBU *vobu, AVIOContext *in, const char *path)
{
    char outname[1024];
    int ret = 0, size;
    int len = vobu->end_sector - 1 - vobu->start_sector;

    snprintf(outname, sizeof(outname),
             "%s/0x%09"PRIx64"-0x%04"PRIx32"-0x%04"PRIx32"%s.vob",
             path,
             vobu->start,
             vobu->dsi.dsi_gi.vobu_c_idn,
             vobu->dsi.dsi_gi.vobu_vob_idn,
             len ? "_d" : "_e");

    av_log(NULL, AV_LOG_WARNING, "0x%08x 0x%04x\n",
           vobu->dsi.dsi_gi.vobu_vob_idn,
           vobu->dsi.dsi_gi.vobu_c_idn);

    if (vobu->dsi.dsi_gi.vobu_c_idn != cell_idn) {
//        av_log(NULL, AV_LOG_WARNING, "cell %d vs %d len %d\n",
//               cell_idn, vobu->dsi.dsi_gi.vobu_c_idn, len);
        cell_idn = vobu->dsi.dsi_gi.vobu_c_idn;
        if (out)
            avio_close(out);
        ret = avio_open(&out, outname, AVIO_FLAG_WRITE);
    }

    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot write %s.\n",
               outname);
        return ret;
    }

    avio_seek(in, vobu->start, SEEK_SET);

    size = vobu->end - vobu->start;

    while (size > 0) {
        uint8_t buf[2048];
        int n;
        n = avio_read(in, buf, sizeof(buf));
        if (n <= 0) {
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

    if (argc < 3)
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

    mkdir(argv[2], 0777);

    for (i = 0; i < nb_vobus; i++) {
        ret = write_vob(vobus + i, in, argv[2]);
        if (ret < 0) {
            exit(1);
        }
    }

    avio_close(out);

    av_free(vobus);

    avio_close(in);

    return 0;
}
