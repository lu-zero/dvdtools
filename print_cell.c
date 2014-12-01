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

static int print_cell(CELL *cell)
{
    av_log(NULL, AV_LOG_INFO,
           "Cell cell_id %d, vob_id %d - first %08x, last %08x\n",
           cell->cell_id,
           cell->vob_id,
           cell->start_sector,
           cell->last_sector);

    return 0;
}

int main(int argc, char *argv[])
{
    AVIOContext *in = NULL;
    VOBU *vobus = NULL;
    CELL *cells = NULL;
    int ret, i = 0, nb_vobus, nb_cells;
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

    nb_cells = populate_cells(&cells, vobus, nb_vobus);

    for (i = 0; i < nb_cells; i++)
        print_cell(&cells[i]);

    av_free(vobus);
    av_free(cells);

    avio_close(in);

    return 0;
}
