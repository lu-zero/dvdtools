#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#include <dvdread/nav_print.h>

#include "common.h"

static int find_next_start_code(AVIOContext *pb, int *size_ptr,
                                int32_t *header_state)
{
    unsigned int state, v;
    int val, n;

    state = *header_state;
    n     = *size_ptr;
    while (n > 0) {
        if (pb->eof_reached)
            break;
        v = avio_r8(pb);
        n--;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val   = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;

found:
    *header_state = state;
    *size_ptr     = n;
    return val;
}
/*
static void print_pci(uint8_t *buf) {
    uint32_t startpts = AV_RB32(buf + 0x0d);
    uint32_t endpts   = AV_RB32(buf + 0x11);
    uint8_t hours     = ((buf[0x19] >> 4) * 10) + (buf[0x19] & 0x0f);
    uint8_t mins      = ((buf[0x1a] >> 4) * 10) + (buf[0x1a] & 0x0f);
    uint8_t secs      = ((buf[0x1b] >> 4) * 10) + (buf[0x1b] & 0x0f);

    printf("startpts %u endpts %u %d:%d:%d\n",
           startpts, endpts, hours, mins, secs);
}

static void print_dsi(uint8_t *buf) {
    uint16_t vob_idn  = buf[6 * 4] << 8 | buf[6 * 4 + 1];
    uint8_t vob_c_idn = buf[6 * 4 + 2];
    uint8_t hours     = ((buf[0x1d] >> 4) * 10) + (buf[0x1d] & 0x0f);
    uint8_t mins      = ((buf[0x1e] >> 4) * 10) + (buf[0x1e] & 0x0f);
    uint8_t secs      = ((buf[0x1f] >> 4) * 10) + (buf[0x1f] & 0x0f);
    printf("vob idn %d c_idn %d %d:%d:%d\n",
           vob_idn, vob_c_idn, hours, mins, secs);
}
*/
void parse_nav_pack(AVIOContext *pb, int32_t *header_state, VOBU *vobu)
{
    int size = MAX_SYNC_SIZE, startcode, len;
    uint8_t pci[NAV_PCI_SIZE];
    uint8_t dsi[NAV_DSI_SIZE];

    avio_read(pb, pci, NAV_PCI_SIZE);
    startcode = find_next_start_code(pb, &size, header_state);
    len = avio_rb16(pb);
    if (startcode != PRIVATE_STREAM_2 ||
        len != NAV_DSI_SIZE) {
        avio_skip(pb, len - 2);
        return;
    }
    avio_read(pb, dsi, NAV_DSI_SIZE);


    navRead_PCI(&vobu->pci, pci + 1);
    navRead_DSI(&vobu->dsi, dsi + 1);

//    navPrint_PCI(&vobu->pci);
//    navPrint_DSI(&vobu->dsi);
    vobu->vob_id  = vobu->dsi.dsi_gi.vobu_vob_idn;
    vobu->cell_id = vobu->dsi.dsi_gi.vobu_c_idn;
}

int find_vobu(AVIOContext *pb, VOBU *vobus, int i)
{
    int size = MAX_SYNC_SIZE, startcode;
    int32_t header_state;

redo:
    header_state = 0xff;
    size = MAX_SYNC_SIZE;
    startcode = find_next_start_code(pb, &size, &header_state);
    if (startcode < 0)
        return AVERROR_EOF;

    if (startcode == PACK_START_CODE ||
        startcode == SYSTEM_HEADER_START_CODE)
        goto redo;

    if (startcode == PRIVATE_STREAM_2) {
        int len = avio_rb16(pb);
        if (len == NAV_PCI_SIZE) {
            vobus[i].start_sector = (avio_tell(pb) - 44) / 2048;
            vobus[i].start        = avio_tell(pb) - 44;
            if (i) {
                vobus[i - 1].end        = vobus[i].start;
                vobus[i - 1].end_sector = vobus[i].start_sector;
            }
            parse_nav_pack(pb, &header_state, &vobus[i]);
            return 0;
         } else {
            avio_skip(pb, len);
            goto redo;
        }
    } else {
        goto redo;
    }
}

int populate_vobs(VOBU **v, const char *filename)
{
    AVIOContext *in = NULL;
    VOBU *vobus = NULL;
    int ret, i = 0, size = 1;
    int64_t end;

    ret = avio_open(&in, filename, AVIO_FLAG_READ);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_ERROR, "Cannot open %s: %s",
               filename, errbuf);
        return -1;
    }

    end = avio_size(in);

    if (av_reallocp_array(&vobus, size, sizeof(VOBU)) < 0)
        return -1;

    while (!find_vobu(in, vobus, i)) {
        if (++i >= size - 1) {
            size *= 2;
            if (av_reallocp_array(&vobus, size, sizeof(VOBU)) < 0)
                return -1;
        }
    }

    if (i) {
        vobus[i - 1].end = end; //FIXME
        vobus[i].start_sector = end / 2048;
        if (vobus[i - 1].vob_id != vobus[i].vob_id ||
            vobus[i - 1].cell_id != vobus[i].cell_id)
            vobus[i - 1].next = 0x3fffffff;
        else
            vobus[i - 1].next = vobus[i - 1].end_sector - vobus[i - 1].start_sector;
        if (i != 1)
            vobus[i + 1].start_sector = -1; //FIXME
        *v = vobus;
    } else {
        av_log(NULL, AV_LOG_ERROR, "Empty %s",
               filename);
        return -1;
    }

    avio_close(in);

    return i;
}
