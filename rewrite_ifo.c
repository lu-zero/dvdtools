#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include <libavutil/intreadwrite.h>
#include <libavformat/avio.h>

#define DVD_BLOCK_LEN 2048

static void help(char *name)
{
    fprintf(stderr, "%s <path> <index> <ifo>\n"
            "path:  Any path supported by dvdnav, device, iso or directory\n"
            "index: The dvd index\n"
            "ifo:   IFO file to write\n",
            name);
    exit(0);
}

typedef struct IFOContext {
    AVClass *class;
    ifo_handle_t *i;
    AVIOContext *pb;
} IFOContext;

IFOContext *ifo_alloc(void)
{
    return av_mallocz(sizeof(IFOContext));
}

static int ifo_open(IFOContext **ifo, const char *path, int rw)
{
    *ifo = ifo_alloc();

    return avio_open(&(*ifo)->pb, path, rw);
}

static void ifo_write_vts_ppt_srp(AVIOContext *pb, int offset,
                                  vts_ptt_srpt_t *srpt)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, srpt->nr_of_srpts);
    avio_wb16(pb, 0);
    avio_wb16(pb, srpt->last_byte);

    for (i = 0; i < srpt->nr_of_srpts; i++)
        avio_wb32(pb, srpt->ttu_offset[i]);
}

static void write_pgci_srp(AVIOContext *pb, pgci_srp_t *pgci)
{
    uint8_t block = pgci->block_mode << 6 | pgci->block_type << 4;
    avio_w8(pb, pgci->entry_id);
    avio_w8(pb, block);
    avio_wb16(pb, pgci->ptl_id_mask);
    avio_wb32(pb, pgci->pgc_start_byte);
}

typedef struct PGCContext {
    uint32_t start_byte;
    pgc_t *pgc;
} PGCContext;

static void write_dvd_time(AVIOContext *pb, dvd_time_t *time)
{

}

static void write_user_ops(AVIOContext *pb, user_ops_t *ops)
{

}

static void write_pgc(AVIOContext *pb, int64_t offset, pgc_t *pgc)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, 0);
    avio_w8(pb, pgc->nr_of_programs);
    avio_w8(pb, pgc->nr_of_cells);

    write_dvd_time(pb, &pgc->playback_time);
    write_user_ops(pb, &pgc->prohibited_ops);

    for (i = 0; i < 8; i++)
        avio_wb16(pb, pgc->audio_control[i]);

    for (i = 0; i < 32; i++)
        avio_wb32(pb, pgc->subp_control[i]);

    avio_wb16(pb, pgc->next_pgc_nr);
    avio_wb16(pb, pgc->prev_pgc_nr);
    avio_wb16(pb, pgc->goup_pgc_nr);

    avio_w8(pb, pgc->still_time);
    avio_w8(pb, pgc->pg_playback_mode);

    for (i = 0; i < 16; i++)
        avio_wb32(pb, pgc->palette[i]);

    avio_wb16(pb, pgc->command_tbl_offset);
    avio_wb16(pb, pgc->program_map_offset);
    avio_wb16(pb, pgc->cell_playback_offset);
    avio_wb16(pb, pgc->cell_position_offset);



}


static void ifo_write_vts_pgcit(AVIOContext *pb, int64_t offset,
                                pgcit_t *pgcit)
{
    int i; //, nb_pgc;
//    int64_t pos = avio_tell(pb);

    avio_seek(pb, offset, SEEK_SET);
    avio_wb16(pb, pgcit->nr_of_pgci_srp);
    avio_wb16(pb, 0);
    avio_wb16(pb, pgcit->last_byte);

    for (i = 0; i < pgcit->nr_of_pgci_srp; i++)
         write_pgci_srp(pb, pgcit->pgci_srp + i);

    for (i = 0; i < pgcit->nr_of_pgci_srp; i++)
         write_pgc(pb, offset + pgcit->pgci_srp[i].pgc_start_byte,
                   pgcit->pgci_srp[i].pgc);
}

static void write_audio_attr(AVIOContext *pb, audio_attr_t *attr)
{

}

static void write_subp_attr(AVIOContext *pb, subp_attr_t *attr)
{

}

static void write_video_attr(AVIOContext *pb, video_attr_t *attr)
{

}

static void write_multichannel_ext(AVIOContext *pb, multichannel_ext_t *ext)
{

}

static void ifo_write_pgci_ut(IFOContext *ifo)
{

}

static void ifo_write_vts_tmapt(IFOContext *ifo)
{

}

static void ifo_write_c_adt(IFOContext *ifo)
{

}

static void ifo_write_vobu_admap(IFOContext *ifo)
{

}

static void ifo_write_title_c_adt(IFOContext *ifo)
{

}

static void ifo_write_title_vobu_admap(IFOContext *ifo)
{

}

static int ifo_write_vts(IFOContext *ifo)
{
    AVIOContext *pb  = ifo->pb;
    vtsi_mat_t *vtsi = ifo->i->vtsi_mat;
    int i;

    avio_printf(ifo->pb, "%s", "DVDVIDEO-VTS");

    avio_wb32(pb, vtsi->vts_last_sector);
    for (i = 0; i < 12; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vtsi->vtsi_last_sector);
    avio_w8(pb, 0);
    avio_w8(pb, vtsi->specification_version);
    avio_wb32(pb, vtsi->vts_category);

    for (i = 0; i < 2 + 2 + 1 + 19 + 2 + 32 + 8 + 24; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vtsi->vtsi_last_byte);

    for (i = 0; i < 4 + 56; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vtsi->vtsm_vobs);
    avio_wb32(pb, vtsi->vtstt_vobs);
    avio_wb32(pb, vtsi->vts_ptt_srpt);
    avio_wb32(pb, vtsi->vts_pgcit);
    avio_wb32(pb, vtsi->vtsm_pgci_ut);
    avio_wb32(pb, vtsi->vts_tmapt);
    avio_wb32(pb, vtsi->vtsm_c_adt);
    avio_wb32(pb, vtsi->vtsm_vobu_admap);
    avio_wb32(pb, vtsi->vts_c_adt);
    avio_wb32(pb, vtsi->vts_vobu_admap);

    for (i = 0; i < 24; i++)
        avio_w8(pb, 0);

    write_video_attr(pb, &vtsi->vtsm_video_attr);
    avio_w8(pb, 0);

    avio_w8(pb, vtsi->nr_of_vtsm_audio_streams);
    write_audio_attr(pb, &vtsi->vtsm_audio_attr);
    for (i = 0; i < 7 * sizeof(audio_attr_t); i++)
        avio_w8(pb, 0);

    for (i = 0; i < 17; i++)
        avio_w8(pb, 0);

    avio_w8(pb, vtsi->nr_of_vtsm_subp_streams);
    write_subp_attr(pb, &vtsi->vtsm_subp_attr);
    for (i = 0; i < 27 * sizeof(subp_attr_t); i++)
        avio_w8(pb, 0);

    for (i = 0; i < 2; i++)
        avio_w8(pb, 0);

    write_video_attr(pb, &vtsi->vts_video_attr);
    avio_w8(pb, 0);

    avio_w8(pb, vtsi->nr_of_vts_audio_streams);
    for (i = 0; i < 8; i++)
        write_audio_attr(pb, vtsi->vts_audio_attr + i);

    for (i = 0; i < 17; i++)
        avio_w8(pb, 0);

    avio_w8(pb, vtsi->nr_of_vts_subp_streams);
    for (i = 0; i < 32; i++)
        write_subp_attr(pb, vtsi->vts_subp_attr + i);

    avio_wb16(pb, 0);

    for (i = 0; i < 8; i++)
        write_multichannel_ext(pb, vtsi->vts_mu_audio_attr + i);

    if (vtsi->vts_ptt_srpt)
        ifo_write_vts_ppt_srp(pb, vtsi->vts_ptt_srpt * DVD_BLOCK_LEN, ifo->i->vts_ptt_srpt);

    if (vtsi->vts_pgcit)
        ifo_write_vts_pgcit(pb, vtsi->vts_pgcit * DVD_BLOCK_LEN, ifo->i->vts_pgcit);

    ifo_write_pgci_ut(ifo);
    ifo_write_vts_tmapt(ifo);
    ifo_write_c_adt(ifo);
    ifo_write_vobu_admap(ifo);

    if (vtsi->vts_c_adt)
        ifo_write_title_c_adt(ifo);
    if (vtsi->vts_vobu_admap)
        ifo_write_title_vobu_admap(ifo);

    avio_flush(ifo->pb);

    return 0;
}

static int ifo_write_vgm(IFOContext *ifo)
{
    return 0;
}

static int ifo_write(IFOContext *ifo, int is_vgm)
{
    if (is_vgm)
        return ifo_write_vgm(ifo);
    else
        return ifo_write_vts(ifo);
}

int main(int argc, char **argv)
{
    IFOContext *ifo = NULL;
    dvd_reader_t *dvd;
    int idx = 0;

    if (argc < 3)
        help(argv[0]);

    dvd = DVDOpen(argv[1]);

    idx = atoi(argv[2]);

    ifo_open(&ifo, argv[3], AVIO_FLAG_WRITE);

    ifo->i = ifoOpen(dvd, idx);

    return ifo_write(ifo, !!idx);
}
