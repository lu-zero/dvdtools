#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include <libavutil/intreadwrite.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#define DVD_BLOCK_LEN 2048

typedef struct PutBitContext {
    uint32_t bit_buf;
    int bit_left;
    uint8_t *buf, *buf_ptr, *buf_end;
    int size_in_bits;
} PutBitContext;

static inline void init_put_bits(PutBitContext *s, uint8_t *buffer,
                                 int buffer_size)
{
    if (buffer_size < 0) {
        buffer_size = 0;
        buffer      = NULL;
    }

    s->size_in_bits = 8 * buffer_size;
    s->buf          = buffer;
    s->buf_end      = s->buf + buffer_size;
    s->buf_ptr      = s->buf;
    s->bit_left     = 32;
    s->bit_buf      = 0;
}

static inline void put_bits(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_left;

    bit_buf  = s->bit_buf;
    bit_left = s->bit_left;

    /* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
    bit_buf |= value << (32 - bit_left);
    if (n >= bit_left) {
        AV_WL32(s->buf_ptr, bit_buf);
        s->buf_ptr += 4;
        bit_buf     = (bit_left == 32) ? 0 : value >> bit_left;
        bit_left   += 32;
    }
    bit_left -= n;
#else
    if (n < bit_left) {
        bit_buf     = (bit_buf << n) | value;
        bit_left   -= n;
    } else {
        bit_buf   <<= bit_left;
        bit_buf    |= value >> (n - bit_left);
        AV_WB32(s->buf_ptr, bit_buf);
        s->buf_ptr += 4;
        bit_left   += 32 - n;
        bit_buf     = value;
    }
#endif

    s->bit_buf  = bit_buf;
    s->bit_left = bit_left;
}

static inline void flush_put_bits(PutBitContext *s)
{
#ifndef BITSTREAM_WRITER_LE
    if (s->bit_left < 32)
        s->bit_buf <<= s->bit_left;
#endif
    while (s->bit_left < 32) {
        /* XXX: should test end of buffer */
#ifdef BITSTREAM_WRITER_LE
        *s->buf_ptr++ = s->bit_buf;
        s->bit_buf  >>= 8;
#else
        *s->buf_ptr++ = s->bit_buf >> 24;
        s->bit_buf  <<= 8;
#endif
        s->bit_left  += 8;
    }
    s->bit_left = 32;
    s->bit_buf  = 0;
}

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
    int i, map_size;

    map_size = (srpt->last_byte + 1 - TT_SRPT_SIZE) / sizeof(int32_t);
    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, srpt->nr_of_srpts);
    avio_wb16(pb, 0);
    avio_wb32(pb, srpt->last_byte);

    for (i = 0; i < srpt->nr_of_srpts; i++) //FIXME HACK!!!
        avio_wb32(pb, srpt->ttu_offset[i]);

    for (; i < map_size; i++)
        avio_wl32(pb, srpt->ttu_offset[i]);
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
    avio_w8(pb, time->hour);
    avio_w8(pb, time->minute);
    avio_w8(pb, time->second);
    avio_w8(pb, time->frame_u);
}

static void write_user_ops(AVIOContext *pb, user_ops_t *ops)
{
    PutBitContext s;
    uint8_t buf[sizeof(*ops)];

    init_put_bits(&s, buf, sizeof(buf));

    put_bits(&s, 7, ops->zero);
    put_bits(&s, 1, ops->video_pres_mode_change);
    put_bits(&s, 1, ops->karaoke_audio_pres_mode_change);
    put_bits(&s, 1, ops->angle_change);
    put_bits(&s, 1, ops->subpic_stream_change);
    put_bits(&s, 1, ops->audio_stream_change);
    put_bits(&s, 1, ops->pause_on);
    put_bits(&s, 1, ops->still_off);
    put_bits(&s, 1, ops->button_select_or_activate);
    put_bits(&s, 1, ops->resume);
    put_bits(&s, 1, ops->chapter_menu_call);
    put_bits(&s, 1, ops->angle_menu_call);
    put_bits(&s, 1, ops->audio_menu_call);
    put_bits(&s, 1, ops->subpic_menu_call);
    put_bits(&s, 1, ops->root_menu_call);
    put_bits(&s, 1, ops->title_menu_call);
    put_bits(&s, 1, ops->backward_scan);
    put_bits(&s, 1, ops->forward_scan);
    put_bits(&s, 1, ops->next_pg_search);
    put_bits(&s, 1, ops->prev_or_top_pg_search);
    put_bits(&s, 1, ops->time_or_chapter_search);
    put_bits(&s, 1, ops->go_up);
    put_bits(&s, 1, ops->stop);
    put_bits(&s, 1, ops->title_play);
    put_bits(&s, 1, ops->chapter_search_or_play);
    put_bits(&s, 1, ops->title_or_time_play);

    flush_put_bits(&s);

    avio_write(pb, buf, sizeof(buf));
}

static void write_command_tbl(AVIOContext *pb, int64_t offset,
                              pgc_command_tbl_t *cmd_tbl)
{
    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, cmd_tbl->nr_of_pre);
    avio_wb16(pb, cmd_tbl->nr_of_post);
    avio_wb16(pb, cmd_tbl->nr_of_cell);
    avio_wl16(pb, cmd_tbl->zero_1); //FIXME HACKISH

    if (cmd_tbl->nr_of_pre)
        avio_write(pb, (uint8_t *)cmd_tbl->pre_cmds,
                   cmd_tbl->nr_of_pre * COMMAND_DATA_SIZE);

    if (cmd_tbl->nr_of_post)
        avio_write(pb, (uint8_t *)cmd_tbl->post_cmds,
                   cmd_tbl->nr_of_post * COMMAND_DATA_SIZE);

    if (cmd_tbl->nr_of_cell)
        avio_write(pb, (uint8_t *)cmd_tbl->cell_cmds,
                   cmd_tbl->nr_of_cell * COMMAND_DATA_SIZE);
}

static void write_pgc_program_map(AVIOContext *pb, int64_t offset,
                                  int nb, pgc_program_map_t *map)
{
    avio_seek(pb, offset, SEEK_SET);

    avio_write(pb, map, nb * sizeof(*map));
}

static void write_cell_playback_internal(AVIOContext *pb,
                                         cell_playback_t *cell)
{
    PutBitContext s;
    uint8_t buf[2];

    init_put_bits(&s, buf, sizeof(buf));

    put_bits(&s, 2, cell->block_mode);
    put_bits(&s, 2, cell->block_type);
    put_bits(&s, 1, cell->seamless_play);
    put_bits(&s, 1, cell->interleaved);
    put_bits(&s, 1, cell->stc_discontinuity);
    put_bits(&s, 1, cell->seamless_angle);
    put_bits(&s, 1, cell->playback_mode);
    put_bits(&s, 1, cell->restricted);
    put_bits(&s, 6, cell->unknown2);

    flush_put_bits(&s);

    avio_write(pb, buf, sizeof(buf));

    avio_w8(pb, cell->still_time);
    avio_w8(pb, cell->cell_cmd_nr);
    write_dvd_time(pb, &cell->playback_time);

    avio_wb32(pb, cell->first_sector);
    avio_wb32(pb, cell->first_ilvu_end_sector);
    avio_wb32(pb, cell->last_vobu_start_sector);
    avio_wb32(pb, cell->last_sector);
}

static void write_cell_playback(AVIOContext *pb, int64_t offset,
                                int nb, cell_playback_t *cell)
{
    int i;
    avio_seek(pb, offset, SEEK_SET);

    for (i = 0; i < nb; i++)
        write_cell_playback_internal(pb, cell + i);
}
static void write_cell_position(AVIOContext *pb, int64_t offset,
                                int nb, cell_position_t *cell)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    for (i = 0; i < nb; i++) {
        avio_wb16(pb, cell[i].vob_id_nr);
        avio_w8(pb, 0);
        avio_w8(pb, cell[i].cell_nr);
    }
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

    if (pgc->command_tbl)
        write_command_tbl(pb, offset + pgc->command_tbl_offset,
                          pgc->command_tbl);

    if (pgc->program_map)
        write_pgc_program_map(pb, offset + pgc->program_map_offset,
                              pgc->nr_of_programs, pgc->program_map);

    if (pgc->cell_playback)
        write_cell_playback(pb, offset + pgc->cell_playback_offset,
                            pgc->nr_of_cells, pgc->cell_playback);

    if (pgc->cell_position)
        write_cell_position(pb, offset + pgc->cell_position_offset,
                            pgc->nr_of_cells, pgc->cell_position);

}


static void ifo_write_pgcit(AVIOContext *pb, int64_t offset,
                            pgcit_t *pgcit)
{
    int i; //, nb_pgc;

    avio_seek(pb, offset, SEEK_SET);
    avio_wb16(pb, pgcit->nr_of_pgci_srp);
    avio_wb16(pb, 0);
    avio_wb32(pb, pgcit->last_byte);

    for (i = 0; i < pgcit->nr_of_pgci_srp; i++)
         write_pgci_srp(pb, pgcit->pgci_srp + i);

    for (i = 0; i < pgcit->nr_of_pgci_srp; i++)
         write_pgc(pb, offset + pgcit->pgci_srp[i].pgc_start_byte,
                   pgcit->pgci_srp[i].pgc);
}

static void write_audio_attr(AVIOContext *pb, audio_attr_t *attr)
{
    uint8_t buffer[sizeof(*attr)];
    PutBitContext s;

    init_put_bits(&s, buffer, sizeof(*attr));

    put_bits(&s, 3, attr->audio_format);
    put_bits(&s, 1, attr->multichannel_extension);
    put_bits(&s, 2, attr->lang_type);
    put_bits(&s, 2, attr->application_mode);

    put_bits(&s, 2, attr->quantization);
    put_bits(&s, 2, attr->sample_frequency);
    put_bits(&s, 1, attr->unknown1);
    put_bits(&s, 3, attr->channels);

    put_bits(&s, 16, attr->lang_code);
    put_bits(&s, 8, attr->lang_extension);
    put_bits(&s, 8, attr->code_extension);
    put_bits(&s, 8, attr->unknown3);

    put_bits(&s, 1, attr->app_info.karaoke.unknown4);
    put_bits(&s, 3, attr->app_info.karaoke.channel_assignment);
    put_bits(&s, 2, attr->app_info.karaoke.version);
    put_bits(&s, 1, attr->app_info.karaoke.mc_intro);
    put_bits(&s, 1, attr->app_info.karaoke.mode);

    flush_put_bits(&s);

    avio_write(pb, buffer, sizeof(*attr));
}

static void write_subp_attr(AVIOContext *pb, subp_attr_t *attr)
{
    uint8_t buffer;
    PutBitContext s;

    init_put_bits(&s, &buffer, 1);
    put_bits(&s, 3, attr->code_mode);
    put_bits(&s, 3, attr->zero1);
    put_bits(&s, 2, attr->type);

    flush_put_bits(&s);
    avio_w8(pb, buffer);

    avio_w8(pb, 0);
    avio_wb16(pb, attr->lang_code);
    avio_w8(pb, attr->lang_extension);
    avio_w8(pb, attr->code_extension);
}

static void write_video_attr(AVIOContext *pb, video_attr_t *attr)
{
    uint8_t buffer[sizeof(*attr)];
    PutBitContext s;

    init_put_bits(&s, buffer, sizeof(*attr));

    put_bits(&s, 2, attr->mpeg_version);
    put_bits(&s, 2, attr->video_format);
    put_bits(&s, 2, attr->display_aspect_ratio);
    put_bits(&s, 2, attr->permitted_df);
    put_bits(&s, 1, attr->line21_cc_1);
    put_bits(&s, 1, attr->line21_cc_2);
    put_bits(&s, 1, attr->unknown1);
    put_bits(&s, 1, attr->bit_rate);
    put_bits(&s, 2, attr->picture_size);
    put_bits(&s, 1, attr->letterboxed);
    put_bits(&s, 1, attr->film_mode);

    flush_put_bits(&s);

    avio_write(pb, buffer, sizeof(*attr));
}

static void write_multichannel_ext(AVIOContext *pb, multichannel_ext_t *ext)
{
    unsigned int i;
    uint8_t buffer[sizeof(*ext)];
    PutBitContext s;

    init_put_bits(&s, buffer, sizeof(*ext));

    put_bits(&s, 7, ext->zero1);
    put_bits(&s, 1, ext->ach0_gme);

    put_bits(&s, 7, ext->zero2);
    put_bits(&s, 1, ext->ach1_gme);

    put_bits(&s, 4, ext->zero3);
    put_bits(&s, 1, ext->ach2_gv1e);
    put_bits(&s, 1, ext->ach2_gv2e);
    put_bits(&s, 1, ext->ach2_gm1e);
    put_bits(&s, 1, ext->ach2_gm2e);

    put_bits(&s, 4, ext->zero4);
    put_bits(&s, 1, ext->ach3_gv1e);
    put_bits(&s, 1, ext->ach3_gv2e);
    put_bits(&s, 1, ext->ach3_gmAe);
    put_bits(&s, 1, ext->ach3_se2e);

    put_bits(&s, 4, ext->zero5);
    put_bits(&s, 1, ext->ach4_gv1e);
    put_bits(&s, 1, ext->ach4_gv2e);
    put_bits(&s, 1, ext->ach4_gmBe);
    put_bits(&s, 1, ext->ach4_seBe);

    for (i = 0; i < 19; i++)
        put_bits(&s, 8, 0);

    flush_put_bits(&s);

    avio_write(pb, buffer, sizeof(*ext));
}

static void write_pgci_lu(AVIOContext *pb, int64_t offset, pgci_lu_t *lu)
{
    avio_wb16(pb, lu->lang_code);
    avio_w8(pb, lu->lang_extension);
    avio_w8(pb, lu->exists);
    avio_wb32(pb, lu->lang_start_byte);

    ifo_write_pgcit(pb, offset + lu->lang_start_byte, lu->pgcit);
}

static void ifo_write_pgci_ut(AVIOContext *pb, int64_t offset,
                              pgci_ut_t *pgci_ut)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, pgci_ut->nr_of_lus);
    avio_wb16(pb, 0);
    avio_wb32(pb, pgci_ut->last_byte);

    for (i = 0; i < pgci_ut->nr_of_lus; i++)
        write_pgci_lu(pb, offset, pgci_ut->lu + i);
}

static void write_tmap(AVIOContext *pb, int64_t offset,
                       vts_tmap_t *tmap)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_w8(pb, tmap->tmu);
    avio_w8(pb, 0);
    avio_wb16(pb, tmap->nr_of_entries);

    for (i = 0; i < tmap->nr_of_entries; i++)
        avio_wb32(pb, tmap->map_ent[i]);
}

static void ifo_write_vts_tmapt(AVIOContext *pb, int64_t offset,
                                vts_tmapt_t *vts_tmapt)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, vts_tmapt->nr_of_tmaps);
    avio_wb16(pb, 0);
    avio_wb32(pb, vts_tmapt->last_byte);

    for (i = 0; i < vts_tmapt->nr_of_tmaps; i++)
        avio_wb32(pb, vts_tmapt->tmap_offset[i]);

    for (i = 0; i < vts_tmapt->nr_of_tmaps; i++)
        write_tmap(pb, offset + vts_tmapt->tmap_offset[i],
                   vts_tmapt->tmap + i);
}

static void ifo_write_c_adt(AVIOContext *pb, int64_t offset,
                            c_adt_t *c_adt)
{
    int i, map_size;

    map_size = (c_adt->last_byte + 1 - C_ADT_SIZE) / sizeof(cell_adr_t);

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, c_adt->nr_of_vobs);
    avio_wb16(pb, 0);
    avio_wb32(pb, c_adt->last_byte);

    for (i = 0; i < map_size; i++) {
        avio_wb16(pb, c_adt->cell_adr_table[i].vob_id);
        avio_w8(pb, c_adt->cell_adr_table[i].cell_id);
        avio_w8(pb, 0);
        avio_wb32(pb, c_adt->cell_adr_table[i].start_sector);
        avio_wb32(pb, c_adt->cell_adr_table[i].last_sector);
    }
}

static void ifo_write_vobu_admap(AVIOContext *pb, int64_t offset,
                                 vobu_admap_t *vobu_admap)
{
    int i, map_size;
    int64_t pos;

    map_size = (vobu_admap->last_byte + 1 - VOBU_ADMAP_SIZE) / sizeof(uint32_t);

    avio_seek(pb, offset, SEEK_SET);

    avio_wb32(pb, vobu_admap->last_byte);

    for (i = 0; i < map_size; i++)
        avio_wb32(pb, vobu_admap->vobu_start_sectors[i]);

    pos = (avio_tell(pb) + DVD_BLOCK_LEN - 1) & (-DVD_BLOCK_LEN);
    for (i = 0; i < pos; i++)
        avio_w8(pb, 0);
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
        ifo_write_pgcit(pb, vtsi->vts_pgcit * DVD_BLOCK_LEN, ifo->i->vts_pgcit);

    ifo_write_pgci_ut(pb, vtsi->vtsm_pgci_ut * DVD_BLOCK_LEN,
                      ifo->i->pgci_ut);
    ifo_write_vts_tmapt(pb, vtsi->vts_tmapt * DVD_BLOCK_LEN,
                        ifo->i->vts_tmapt);

    if (ifo->i->menu_c_adt)
        ifo_write_c_adt(pb, vtsi->vtsm_c_adt * DVD_BLOCK_LEN,
                        ifo->i->menu_c_adt);
    if (ifo->i->menu_vobu_admap)
        ifo_write_vobu_admap(pb, vtsi->vtsm_vobu_admap * DVD_BLOCK_LEN,
                             ifo->i->menu_vobu_admap);

    if (vtsi->vts_c_adt)
        ifo_write_c_adt(pb, vtsi->vts_c_adt * DVD_BLOCK_LEN,
                        ifo->i->vts_c_adt);
    if (vtsi->vts_vobu_admap)
        ifo_write_vobu_admap(pb, vtsi->vts_vobu_admap * DVD_BLOCK_LEN,
                             ifo->i->vts_vobu_admap);


    avio_flush(ifo->pb);

    avio_close(ifo->pb);

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

    av_register_all();

    if (argc < 3)
        help(argv[0]);

    dvd = DVDOpen(argv[1]);

    idx = atoi(argv[2]);

    ifo_open(&ifo, argv[3], AVIO_FLAG_READ_WRITE);

    ifo->i = ifoOpen(dvd, idx);

    return ifo_write(ifo, !idx);
}
