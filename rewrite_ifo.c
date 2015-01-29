#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include <libavutil/intreadwrite.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "common.h"

typedef struct PutBitContext {
    uint32_t bit_buf;
    int bit_left;
    uint8_t *buf, *buf_ptr, *buf_end;
    int size_in_bits;
} PutBitContext;

#define avio_seek(a, b, c)                                              \
    do {                                                                \
        int64_t _next = b;                                              \
        int64_t _pos  = avio_tell(a);                                   \
        av_log(NULL, AV_LOG_INFO | AV_LOG_C(214),                       \
               "%s %d: Seek to 0x%08" PRIx64 " from 0x%08" PRIx64 "\n", \
               __FUNCTION__,                                            \
               __LINE__,                                                \
               _next, _pos);                                            \
        avio_seek(a, b, c);                                             \
    } while (0)

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
    fprintf(stderr, "%s <src_path> <dst_path> <index>\n"
            "src_path:  The path to a dvd-video file layout, unencrypted\n"
            "dst_path:  The path to a dvd-video file layout, with unified VOB files.\n"
            "index:     The index of the ifo to patch\n",
            name);
    exit(0);
}

typedef struct IFOContext {
    ifo_handle_t *i;
    AVIOContext *pb;
    int64_t ifo_size;
} IFOContext;

IFOContext *ifo_alloc(void)
{
    return av_mallocz(sizeof(IFOContext));
}

static int64_t ifo_size(const char *path, int idx)
{
    char ifo_path[1024];
    struct stat st;

    if (!idx) {
        snprintf(ifo_path, sizeof(ifo_path),
                 "%s/VIDEO_TS/%s.IFO", path, "VIDEO_TS");
    } else {
        snprintf(ifo_path, sizeof(ifo_path),
                 "%s/VIDEO_TS/VTS_%02d_0.IFO", path, idx);
    }

    stat(ifo_path, &st);

    return st.st_size;
}

static int64_t menu_size(const char *path, int idx)
{
    char menu_path[1024];
    struct stat st;

    if (!idx) {
        snprintf(menu_path, sizeof(menu_path),
                 "%s/VIDEO_TS/%s.VOB", path, "VIDEO_TS");
    } else {
        snprintf(menu_path, sizeof(menu_path),
                 "%s/VIDEO_TS/VTS_%02d_0.VOB", path, idx);
    }

    // Menu VOBs can be omitted
    if (stat(menu_path, &st) < 0)
        return 0;

    return st.st_size;
}

static int64_t title_size(const char *path, int idx)
{
    char title_path[1024];
    struct stat st;
    if (!idx)
        return 0;

    snprintf(title_path, sizeof(title_path),
             "%s/VIDEO_TS/VTS_%02d_1.VOB", path, idx);

    stat(title_path, &st);

    return st.st_size;
}

static int to_sector(int64_t size)
{
    return (size + DVD_BLOCK_LEN - 1) / DVD_BLOCK_LEN;
}

// ifo size is the src one by design
static int title_set_sector(const char *src, const char *dst, int idx)
{
    int ifo_sector   = to_sector(ifo_size(src, idx));
    int menu_sector  = to_sector(menu_size(dst, idx));
    int title_sector = to_sector(title_size(dst, idx));

    av_log(NULL, AV_LOG_INFO|AV_LOG_C(211),
           "ifo 0x%08d menu 0x%08d title 0x%08d\n",
           ifo_sector,
           menu_sector,
           title_sector);
    return 2 * ifo_sector + menu_sector + title_sector;
}

static int ifo_open_internal(AVIOContext **pb,
                             const char *path,
                             const char *ext,
                             int idx,
                             int rw)
{
    char ifo_path[1024];

    if (!idx) {
        snprintf(ifo_path, sizeof(ifo_path),
                 "%s/VIDEO_TS/%s.%s", path, "VIDEO_TS", ext);
    } else {
        snprintf(ifo_path, sizeof(ifo_path),
                 "%s/VIDEO_TS/VTS_%02d_0.%s", path, idx, ext);
    }

    return avio_open(pb, ifo_path, rw);
}

static int ifo_open(IFOContext **ifo,
                    const char *path,
                    int idx,
                    int rw)
{
    *ifo = ifo_alloc();
    if (!ifo)
        return AVERROR(ENOMEM);

    return ifo_open_internal(&(*ifo)->pb, path, "IFO", idx, rw);
}

static void ifo_write_vts_ptt_srp(AVIOContext *pb, int offset,
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
    int64_t pos;

    avio_wb16(pb, lu->lang_code);
    avio_w8(pb, lu->lang_extension);
    avio_w8(pb, lu->exists);
    avio_wb32(pb, lu->lang_start_byte);

    pos = avio_tell(pb);
    ifo_write_pgcit(pb, offset + lu->lang_start_byte, lu->pgcit);
    avio_seek(pb, pos, SEEK_SET);
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

void patch_cell_playback(cell_playback_t *cell_playback, CELL *cell)
{
    cell_playback->first_sector           = cell->start_sector;
    cell_playback->last_sector            = cell->last_sector;
    cell_playback->last_vobu_start_sector = cell->last_vobu_start_sector;
}

CELL *match_cell(CELL *cells, int nb_cells, int vob_id, int cell_id)
{
    int i;

    for (i = 0; i <= nb_cells; i++) {
        if (cells[i].vob_id == vob_id &&
            cells[i].cell_id == cell_id)
            return cells + i;
    }

    av_log(NULL, AV_LOG_ERROR, "Cannot find cell %d-%d\n",
           vob_id, cell_id);

    exit(1);

    return NULL;
}

static void patch_pgc(pgc_t *pgc, CELL *cells, int nb_cells)
{
    int i;
    if (!pgc->cell_playback)
        return;

    for (i = 0; i < pgc->nr_of_cells; i++) {
        CELL *cell = match_cell(cells, nb_cells,
                                pgc->cell_position[i].vob_id_nr,
                                pgc->cell_position[i].cell_nr);
        patch_cell_playback(pgc->cell_playback + i, cell);
    }
}

static void patch_pgcit(pgcit_t *pgcit, CELL *cells, int nb_cells)
{
    int i;

    for (i = 0; i < pgcit->nr_of_pgci_srp; i++)
        patch_pgc(pgcit->pgci_srp[i].pgc, cells, nb_cells);
}

static void patch_pgci_lu(pgci_lu_t *lu, CELL *cells, int nb_cells)
{
    patch_pgcit(lu->pgcit, cells, nb_cells);
}

static void patch_pgci_ut(pgci_ut_t *pgci_ut, CELL *cells, int nb_cells)
{
    int i;

    for (i = 0; i < pgci_ut->nr_of_lus; i++)
        patch_pgci_lu(pgci_ut->lu + i, cells, nb_cells);
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

    map_size = (vobu_admap->last_byte + 1 - VOBU_ADMAP_SIZE) / sizeof(uint32_t);

    avio_seek(pb, offset, SEEK_SET);

    avio_wb32(pb, vobu_admap->last_byte);

    for (i = 0; i < map_size; i++)
        avio_wb32(pb, vobu_admap->vobu_start_sectors[i]);
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
        ifo_write_vts_ptt_srp(pb, vtsi->vts_ptt_srpt * DVD_BLOCK_LEN, ifo->i->vts_ptt_srpt);

    if (vtsi->vts_pgcit)
        ifo_write_pgcit(pb, vtsi->vts_pgcit * DVD_BLOCK_LEN, ifo->i->vts_pgcit);

    ifo_write_pgci_ut(pb, vtsi->vtsm_pgci_ut * DVD_BLOCK_LEN,
                      ifo->i->pgci_ut);

    if (ifo->i->vts_tmapt)
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

    return 0;
}

static void write_playback_type(AVIOContext *pb,
                                playback_type_t *pt)
{
    PutBitContext s;
    uint8_t buf[sizeof(*pt)];

    init_put_bits(&s, buf, sizeof(buf));
    put_bits(&s, 1, pt->zero_1                   );
    put_bits(&s, 1, pt->multi_or_random_pgc_title);
    put_bits(&s, 1, pt->jlc_exists_in_cell_cmd   );
    put_bits(&s, 1, pt->jlc_exists_in_prepost_cmd);
    put_bits(&s, 1, pt->jlc_exists_in_button_cmd );
    put_bits(&s, 1, pt->jlc_exists_in_tt_dom     );
    put_bits(&s, 1, pt->chapter_search_or_play   );
    put_bits(&s, 1, pt->title_or_time_play       );

    flush_put_bits(&s);

    avio_write(pb, buf, sizeof(buf));
}

static void write_tt_srpt(AVIOContext *pb, int64_t offset,
                          tt_srpt_t *tt_srpt)
{
    int i, map_size;

    map_size = (tt_srpt->last_byte + 1 - TT_SRPT_SIZE) / sizeof(title_info_t);

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, tt_srpt->nr_of_srpts);
    avio_wb16(pb, 0);
    avio_wb32(pb, tt_srpt->last_byte);

    for (i = 0; i < map_size; i++) {
        write_playback_type(pb, &tt_srpt->title[i].pb_ty);
        avio_w8(pb, tt_srpt->title[i].nr_of_angles);
        avio_wb16(pb, tt_srpt->title[i].nr_of_ptts);
        avio_wb16(pb, tt_srpt->title[i].parental_id);
        avio_w8(pb, tt_srpt->title[i].title_set_nr);
        avio_w8(pb, tt_srpt->title[i].vts_ttn);
        avio_wb32(pb, tt_srpt->title[i].title_set_sector);
    }
}

static void write_ptl_mait_country(AVIOContext *pb,
                                   ptl_mait_country_t *country)
{
    avio_wb16(pb, country->country_code);
    avio_wb16(pb, 0);
    avio_wb16(pb, country->pf_ptl_mai_start_byte);
    avio_wb16(pb, 0);
}

static void write_pf_level(AVIOContext *pb, int64_t offset,
                           pf_level_t *pf,
                           int nr_of_vtss)
{
    int level, vts, map_size, i;
    int16_t *pf_temp;

    map_size = (nr_of_vtss + 1) * sizeof(pf_level_t);

    pf_temp = av_malloc(map_size);

    avio_seek(pb, offset, SEEK_SET);

    for (level = 0; level < PTL_MAIT_NUM_LEVEL; level++) {
        for (vts = 0; vts <= nr_of_vtss; vts++) {
            pf_temp[(7 - level) * (nr_of_vtss + 1) + vts] = pf[vts][level];
        }
    }

    for (i = 0; i < map_size; i++)
        avio_wb16(pb, pf_temp[i]);
    av_free(pf_temp);
}

static void write_ptl_mait(AVIOContext *pb, int64_t offset,
                           ptl_mait_t *ptl_mait)
{
    int i;

    // map_size = (ptl_mait->last_byte + 1 - PTL_MAIT_SIZE) / PTL_MAIT_COUNTRY_SIZE;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, ptl_mait->nr_of_countries);
    avio_wb16(pb, ptl_mait->nr_of_vtss);
    avio_wb32(pb, ptl_mait->last_byte);

    for (i = 0; i < ptl_mait->nr_of_countries; i++) {
        write_ptl_mait_country(pb, ptl_mait->countries + i);
    }
    for (i = 0; i < ptl_mait->nr_of_countries; i++) {
        write_pf_level(pb,
                       offset + ptl_mait->countries[i].pf_ptl_mai_start_byte,
                       ptl_mait->countries[i].pf_ptl_mai,
                       ptl_mait->nr_of_vtss);
    }
}

static void write_vts_attribute(AVIOContext *pb, int64_t offset,
                                vts_attributes_t *vts_attributes)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb32(pb, vts_attributes->last_byte);
    avio_wb32(pb, vts_attributes->vts_cat);
    write_video_attr(pb, &vts_attributes->vtsm_vobs_attr);
    avio_w8(pb, 0);
    avio_w8(pb, vts_attributes->nr_of_vtsm_audio_streams);
    write_audio_attr(pb, &vts_attributes->vtsm_audio_attr);
    for (i = 0; i < 7 * sizeof(audio_attr_t); i++)
        avio_w8(pb, 0);
    for (i = 0; i < 16; i++)
        avio_w8(pb, 0);
    avio_w8(pb, 0);
    avio_w8(pb, vts_attributes->nr_of_vtsm_subp_streams);
    write_subp_attr(pb, &vts_attributes->vtsm_subp_attr);
    for (i = 0; i < 27 * sizeof(subp_attr_t); i++)
        avio_w8(pb, 0);
    avio_wb16(pb, 0);
    write_video_attr(pb, &vts_attributes->vtstt_vobs_video_attr);
    avio_w8(pb, 0);
    avio_w8(pb, vts_attributes->nr_of_vtstt_audio_streams);
    for (i = 0; i < 8; i++)
        write_audio_attr(pb, &vts_attributes->vtstt_audio_attr[i]);
    for (i = 0; i < 16; i++)
        avio_w8(pb, 0);
    avio_w8(pb, 0);
    avio_w8(pb, vts_attributes->nr_of_vtstt_subp_streams);
    for (i = 0; i < 32; i++)
         write_subp_attr(pb, &vts_attributes->vtstt_subp_attr[i]);
}

static void write_vts_atrt(AVIOContext *pb, int64_t offset,
                           vts_atrt_t *vts_atrt)
{
    int i;

    avio_seek(pb, offset, SEEK_SET);

    avio_wb16(pb, vts_atrt->nr_of_vtss);
    avio_wb16(pb, 0);
    avio_wb32(pb, vts_atrt->last_byte);

    for (i = 0; i < vts_atrt->nr_of_vtss; i++)
        avio_wb32(pb, vts_atrt->vts_atrt_offsets[i]);

    for (i = 0; i < vts_atrt->nr_of_vtss; i++)
        write_vts_attribute(pb, offset + vts_atrt->vts_atrt_offsets[i],
                            vts_atrt->vts + i);
}

static void write_txtdt_mgi(AVIOContext *pb, int64_t offset,
                            txtdt_mgi_t *txtdt_mgi)
{
    avio_seek(pb, offset, SEEK_SET);

    avio_write(pb, (uint8_t *)txtdt_mgi, sizeof(*txtdt_mgi)); //FIXME
}

static int ifo_write_vgm(IFOContext *ifo)
{
    AVIOContext *pb  = ifo->pb;
    vmgi_mat_t *vmgi = ifo->i->vmgi_mat;
    int i;

    avio_printf(pb, "%s", "DVDVIDEO-VMG");

    avio_wb32(pb, vmgi->vmg_last_sector);

    for (i = 0; i < 12; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vmgi->vmgi_last_sector);
    avio_w8(pb, 0);

    avio_w8(pb, vmgi->specification_version);
    avio_wb32(pb, vmgi->vmg_category);
    avio_wb16(pb, vmgi->vmg_nr_of_volumes);
    avio_wb16(pb, vmgi->vmg_this_volume_nr);

    avio_w8(pb, vmgi->disc_side);

    for (i = 0; i < 19; i++)
        avio_w8(pb, 0);

    avio_wb16(pb, vmgi->vmg_nr_of_title_sets);
    avio_write(pb, (uint8_t *)vmgi->provider_identifier, 32);

    avio_wb64(pb, vmgi->vmg_pos_code);

    for (i = 0; i < 24; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vmgi->vmgi_last_byte);
    avio_wb32(pb, vmgi->first_play_pgc);

    for (i = 0; i < 56; i++)
        avio_w8(pb, 0);

    avio_wb32(pb, vmgi->vmgm_vobs);
    avio_wb32(pb, vmgi->tt_srpt);
    avio_wb32(pb, vmgi->vmgm_pgci_ut);
    avio_wb32(pb, vmgi->ptl_mait);
    avio_wb32(pb, vmgi->vts_atrt);
    avio_wb32(pb, vmgi->txtdt_mgi);
    avio_wb32(pb, vmgi->vmgm_c_adt);
    avio_wb32(pb, vmgi->vmgm_vobu_admap);

    for (i = 0; i < 32; i++)
        avio_w8(pb, 0);

    write_video_attr(pb, &vmgi->vmgm_video_attr);
    avio_w8(pb, 0);
    avio_w8(pb, vmgi->nr_of_vmgm_audio_streams);

    write_audio_attr(pb, &vmgi->vmgm_audio_attr);
    for (i = 0; i < 7 * sizeof(audio_attr_t); i++)
        avio_w8(pb, 0);

    for (i = 0; i < 17; i++)
        avio_w8(pb, 0);

    avio_w8(pb, vmgi->nr_of_vmgm_subp_streams);

    write_subp_attr(pb, &vmgi->vmgm_subp_attr);
    for (i = 0; i < 27 * sizeof(subp_attr_t); i++)
        avio_w8(pb, 0);

    if (vmgi->first_play_pgc)
        write_pgc(pb, vmgi->first_play_pgc,
                  ifo->i->first_play_pgc);

    write_tt_srpt(pb, vmgi->tt_srpt * DVD_BLOCK_LEN,
                  ifo->i->tt_srpt);

    ifo_write_pgci_ut(pb, vmgi->vmgm_pgci_ut * DVD_BLOCK_LEN,
                      ifo->i->pgci_ut);

    if (vmgi->ptl_mait)
        write_ptl_mait(pb, vmgi->ptl_mait * DVD_BLOCK_LEN,
                       ifo->i->ptl_mait);

    write_vts_atrt(pb, vmgi->vts_atrt * DVD_BLOCK_LEN,
                   ifo->i->vts_atrt);

    if (vmgi->txtdt_mgi)
        write_txtdt_mgi(pb, vmgi->txtdt_mgi * DVD_BLOCK_LEN,
                        ifo->i->txtdt_mgi);

    if (vmgi->vmgm_c_adt)
        ifo_write_c_adt(pb, vmgi->vmgm_c_adt * DVD_BLOCK_LEN,
                        ifo->i->menu_c_adt);

    if (vmgi->vmgm_vobu_admap)
        ifo_write_vobu_admap(pb, vmgi->vmgm_vobu_admap * DVD_BLOCK_LEN,
                             ifo->i->menu_vobu_admap);

    return 0;
}

static int ifo_write(IFOContext *ifo,
                     const char *src_path,
                     const char *dst_path,
                     int idx)
{
    int64_t pos;
    int ret, i, len;
    int ifo_last_sector, bup_last_sector;
    int menu_sector, title_sector, ifo_sector;

    if (idx)
        ret = ifo_write_vts(ifo);
    else
        ret = ifo_write_vgm(ifo);

    len = FFMAX(ifo->ifo_size - avio_tell(ifo->pb), 0);

    for (i = 0; i < len; i++)
        avio_w8(ifo->pb, 0);

    pos = avio_tell(ifo->pb);
    len = (pos + DVD_BLOCK_LEN - 1) / DVD_BLOCK_LEN;

    ifo_last_sector = len - 1;

    ifo_sector   = to_sector(ifo_size(src_path, idx));
    menu_sector  = to_sector(menu_size(dst_path, idx));
    title_sector = to_sector(title_size(dst_path, idx));

    av_log(NULL, AV_LOG_INFO|AV_LOG_C(111),
           "ifo %d vs %d, menu %d title %d\n",
           ifo_sector, ifo_last_sector,
           menu_sector, title_sector);

    bup_last_sector = title_set_sector(src_path, dst_path, idx);

    if (ifo->i->vtsi_mat) {
        av_log(NULL, AV_LOG_WARNING, "last_sector (vts) %08x %08x\n",
               ifo->i->vtsi_mat->vts_last_sector,
               ifo->i->vtsi_mat->vtsi_last_sector);
        ifo->i->vtsi_mat->vts_last_sector  = bup_last_sector - 1;
        ifo->i->vtsi_mat->vtsi_last_sector = ifo_sector - 1;
        if (ifo->i->menu_c_adt) // FIXME doublecheck
            ifo->i->vtsi_mat->vtsm_vobs = ifo_sector;
        else
            ifo->i->vtsi_mat->vtsm_vobs = 0;

        ifo->i->vtsi_mat->vtstt_vobs = ifo_sector + menu_sector;
    }

    if (ifo->i->vmgi_mat) {
        av_log(NULL, AV_LOG_WARNING, "last_sector (vmg) %08x %08x\n",
               ifo->i->vmgi_mat->vmg_last_sector,
               ifo->i->vmgi_mat->vmgi_last_sector);
        ifo->i->vmgi_mat->vmg_last_sector  = bup_last_sector - 1;
        ifo->i->vmgi_mat->vmgi_last_sector = ifo_sector - 1;
        ifo->i->vmgi_mat->vmgm_vobs        = ifo_sector;
    }

    // FIXME we are writing twice, we could update just the values
    // now that we know them.
    avio_seek(ifo->pb, 0, SEEK_SET);

    if (idx)
        ret = ifo_write_vts(ifo);
    else
        ret = ifo_write_vgm(ifo);

    len = FFMAX(ifo->ifo_size - avio_tell(ifo->pb), 0);

    for (i = 0; i < len; i++)
        avio_w8(ifo->pb, 0xff);

    avio_flush(ifo->pb);

    avio_close(ifo->pb);

    return ret;
}

static void patch_tt_srpt(IFOContext *ifo,

                          const char *src_path,
                          const char *dst_path)
{
    vmgi_mat_t *vmgi_mat   = ifo->i->vmgi_mat;
    tt_srpt_t *tt_srpt     = ifo->i->tt_srpt;
    int i, sector          = 0;
    int32_t *title_sectors = av_malloc(vmgi_mat->vmg_nr_of_title_sets *
                                       sizeof(int32_t));

    for (i = 0; i < vmgi_mat->vmg_nr_of_title_sets; i++) {
        sector += title_set_sector(src_path, dst_path, i);
        title_sectors[i] = sector;
    }

    for (i = 0; i < tt_srpt->nr_of_srpts; i++) {
        sector = title_sectors[tt_srpt->title[i].title_set_nr - 1];
        av_log(NULL, AV_LOG_INFO, "title_set_sector %d ",
               tt_srpt->title[i].title_set_nr - 1);
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(121),
               "0x%08x -> 0x%08x\n",
               tt_srpt->title[i].title_set_sector,
               sector);
        tt_srpt->title[i].title_set_sector = sector;
    }
}

static void patch_c_adt(c_adt_t *c_adt, CELL *cells, int nb_cells)
{
    int i, map_size;

    map_size = (c_adt->last_byte + 1 - C_ADT_SIZE) / sizeof(cell_adr_t);



    for (i = 0; i < map_size; i++) {
        CELL *c = match_cell(cells, nb_cells,
                             c_adt->cell_adr_table[i].vob_id,
                             c_adt->cell_adr_table[i].cell_id);

        av_log(NULL, AV_LOG_INFO, "vob_id %d, cell_id %d",
               c_adt->cell_adr_table[i].vob_id,
               c_adt->cell_adr_table[i].cell_id);

        av_log(NULL, AV_LOG_INFO|AV_LOG_C(111), "s 0x%08x, 0x%08x -> ",
               c_adt->cell_adr_table[i].start_sector,
               c_adt->cell_adr_table[i].last_sector);

        c_adt->cell_adr_table[i].start_sector = c->start_sector;
        c_adt->cell_adr_table[i].last_sector  = c->last_sector;
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(111), "s 0x%08x, 0x%08x\n",
               c_adt->cell_adr_table[i].start_sector,
               c_adt->cell_adr_table[i].last_sector);
    }
}

void patch_vobu_admap(vobu_admap_t *vobu_admap, VOBU *vobus, int nb_vobus)
{
    int i, map_size;

    map_size = (vobu_admap->last_byte + 1 - VOBU_ADMAP_SIZE) / sizeof(uint32_t);

    if (map_size > nb_vobus + 1) {
        av_log(NULL, AV_LOG_ERROR,
               "The number of vobus %d is less than %d cannot patch admap,\n",
               nb_vobus, map_size);
//        exit(1); XXX HACK!
        map_size = nb_vobus + 1;
        vobu_admap->last_byte = VOBU_ADMAP_SIZE - 1 + map_size;
    }

    for (i = 0; i < map_size; i++)
        vobu_admap->vobu_start_sectors[i] = vobus[i].start_sector;
}

int fix_title(IFOContext *ifo, const char* path, int idx)
{
    char title[1024];
    VOBU *vobus;
    int nb_vobus;
    CELL *cells;
    int nb_cells;

    snprintf(title, sizeof(title), "%s/VIDEO_TS/VTS_%02d_1.VOB", path, idx);

    if ((nb_vobus = populate_vobs(&vobus, title)) < 0)
        return -1;

    if ((nb_cells = populate_cells(&cells, vobus, nb_vobus)) < 0)
        return -1;

    if (ifo->i->vts_c_adt)
        patch_c_adt(ifo->i->vts_c_adt, cells, nb_cells);

    if (ifo->i->vts_vobu_admap)
        patch_vobu_admap(ifo->i->vts_vobu_admap, vobus, nb_vobus);

    patch_pgcit(ifo->i->vts_pgcit, cells, nb_cells);

    return 0;
}

int fix_menu(IFOContext *ifo, const char *path, int idx)
{
    char menu[1024];
    VOBU *vobus;
    int nb_vobus;
    CELL *cells;
    int nb_cells;

    if (idx)
        snprintf(menu, sizeof(menu), "%s/VIDEO_TS/VTS_%02d_0.VOB", path, idx);
    else
        snprintf(menu, sizeof(menu), "%s/VIDEO_TS/VIDEO_TS.VOB", path);

    if ((nb_vobus = populate_vobs(&vobus, menu)) < 0)
        return -1;

    if ((nb_cells = populate_cells(&cells, vobus, nb_vobus)) < 0)
        return -1;

    if (ifo->i->menu_c_adt)
        patch_c_adt(ifo->i->menu_c_adt, cells, nb_cells);

    if (ifo->i->menu_vobu_admap)
        patch_vobu_admap(ifo->i->menu_vobu_admap, vobus, nb_vobus);

    patch_pgci_ut(ifo->i->pgci_ut, cells, nb_cells);

    return 0;
}

int main(int argc, char **argv)
{
    IFOContext *ifo = NULL;
    dvd_reader_t *dvd;
    int ret, idx = 0;
    const char *src_path, *dst_path;

    av_register_all();

    if (argc < 4)
        help(argv[0]);

    src_path = argv[1];
    dst_path = argv[2];
    idx = atoi(argv[3]);

    ifo_open(&ifo, dst_path, idx, AVIO_FLAG_READ_WRITE);

    dvd = DVDOpen(src_path);

    ifo->ifo_size = ifo_size(src_path, idx);

    ifo->i = ifoOpen(dvd, idx);

    if (!idx)
        patch_tt_srpt(ifo, src_path, dst_path);

    if (idx) {
        ret = fix_title(ifo, dst_path, idx);
        if (ret < 0)
            return ret;
    }

    ret = fix_menu(ifo, dst_path, idx);
//    if (ret < 0)
//        return ret;

    return ifo_write(ifo, src_path, dst_path, idx);
}
