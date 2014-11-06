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

void write_vob(VOBU *vobu, AVIOContext *in)
{
    AVIOContext *out = NULL;
    char outname[1024];
    int ret, size;

    snprintf(outname, sizeof(outname), "tmp-0x%08"PRIx32".vob", vobu->sector);

    ret = avio_open(&out, outname, AVIO_FLAG_WRITE);

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
    avio_close(out);
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
        write_vob(vobus + i, in);
    }

    av_free(vobus);

    avio_close(in);

    return 0;
}
