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
            "vob: A full VOB file.\n"
            "outpath: output path.\n",
            name);
    exit(0);
}

AVIOContext *out = NULL;
int vob_idn = -1;
static int write_cell(CELL *cell, AVIOContext *in, const char *path)
{
    char outname[1024];
    int ret = 0;
    int64_t size;

    snprintf(outname, sizeof(outname),
             "%s/0x%08"PRIx32"-0x%04"PRIx32"-0x%04"PRIx32".vob",
             path,
             cell->start_sector * DVD_BLOCK_LEN,
             cell->cell_id,
             cell->vob_id);

    if (cell->vob_id != vob_idn) {
        vob_idn = cell->vob_id;
        if (out)
            avio_close(out);
        ret = avio_open(&out, outname, AVIO_FLAG_WRITE);
    }

    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot write %s.\n",
               outname);
        return ret;
    }

    avio_seek(in, cell->start_sector * DVD_BLOCK_LEN, SEEK_SET);

    size = (1 + cell->last_sector - cell->start_sector) * DVD_BLOCK_LEN;

    printf("Cell size %ld %d %d\n", size, cell->last_sector, cell->start_sector);

    while (size > 0) {
        uint8_t buf[DVD_BLOCK_LEN];
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
    CELL *cells = NULL;
    int ret, i = 0, nb_vobus, nb_cells;
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

    nb_cells = populate_cells(&cells, vobus, nb_vobus);


    mkdir(argv[2], 0777);

    for (i = 0; i < nb_cells; i++) {
        ret = write_cell(cells + i, in, argv[2]);
        if (ret < 0) {
            exit(1);
        }
    }

    avio_close(out);

    av_free(vobus);
    av_free(cells);

    avio_close(in);

    return 0;
}
