#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "common.h"

static void help(char *name)
{
    fprintf(stderr, "%s <src_path> <dst_path> <index>\n"
            "src_path:  The path to a dvd-video unified file layout, unencrypted\n"
            "dst_path:  The path the output directory\n"
            "index:     The index of the title to fix\n",
            name);
    exit(0);
}

static int fix_vob(cell_adr_t *cell_adr_table,
                   const char *src_path,
                   const char *dst_path)
{
    VOBU *vobus;
    int nb_vobus;
    CELL *cells;
    int nb_cells, i;

    if ((nb_vobus = populate_vobs(&vobus, src_path)) < 0)
        return -1;

    if ((nb_cells = populate_cells(&cells, vobus, nb_vobus)) < 0)
        return -1;

    for (i = 0; i < nb_cells; i++) {
         av_log(NULL, AV_LOG_INFO|AV_LOG_C(128),
                "vob_id %02d, cell_id %02d, off 0x%08x",
                cell_adr_table[i].vob_id,
                cell_adr_table[i].cell_id,
                cell_adr_table[i].start_sector);

        av_log(NULL, AV_LOG_INFO, " -> ");

        av_log(NULL, AV_LOG_INFO|AV_LOG_C(111),
                "vob_id %02d, cell_id %02d, off 0x%08x",
                cells[i].vob_id,
                cells[i].cell_id,
                cells[i].start_sector);
        if (cell_adr_table[i].vob_id != cells[i].vob_id ||
            cell_adr_table[i].cell_id != cells[i].cell_id)
            av_log(NULL, AV_LOG_INFO|AV_LOG_C(222),
                   " X ");
        av_log(NULL, AV_LOG_INFO, "\n");
    }

    return 0;
}

static int fix_title_vob(ifo_handle_t *ifo,
                         const char *src_path,
                         const char *dst_path,
                         int idx)
{
    char src[1024];
    char dst[1024];

    snprintf(src, sizeof(src), "%s/VIDEO_TS/VTS_%02d_1.VOB",
             src_path, idx);

    snprintf(dst, sizeof(dst), "%s/VIDEO_TS/VTS_%02d_1.VOB",
             dst_path, idx);

    return fix_vob(ifo->vts_c_adt->cell_adr_table, src, dst);
}

static int fix_menu_vob(ifo_handle_t *ifo,
                        const char *src_path,
                        const char *dst_path,
                        int idx)
{
    char src[1024];
    char dst[1024];


    if (idx) {
        snprintf(src, sizeof(src), "%s/VIDEO_TS/VTS_%02d_0.VOB",
                 src_path, idx);

        snprintf(dst, sizeof(dst), "%s/VIDEO_TS/VTS_%02d_0.VOB",
                 dst_path, idx);
    } else {
        snprintf(src, sizeof(src), "%s/VIDEO_TS/VIDEO_TS.VOB",
                 src_path);
        snprintf(dst, sizeof(dst), "%s/VIDEO_TS/VIDEO_TS.VOB",
                 dst_path);
    }

    return fix_vob(ifo->menu_c_adt->cell_adr_table, src, dst);
}

int main(int argc, char **argv)
{
    dvd_reader_t *dvd;
    ifo_handle_t *ifo;
    int idx = 0;
    const char *src_path, *dst_path;

    av_register_all();

    if (argc < 4)
        help(argv[0]);

    src_path = argv[1];
    dst_path = argv[2];
    idx = atoi(argv[3]);

    dvd = DVDOpen(src_path);

    ifo = ifoOpen(dvd, idx);

    fix_menu_vob(ifo, src_path, dst_path, idx);

    if (idx)
        fix_title_vob(ifo, src_path, dst_path, idx);

    return 0;
}
