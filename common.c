#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

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

void parse_nav_pack(AVIOContext *pb, int32_t *header_state, VOBU *vobu)
{
    int size = MAX_SYNC_SIZE, startcode, len;
    uint8_t pci[NAV_PCI_SIZE];
    uint8_t dsi[NAV_DSI_SIZE];

    avio_read(pb, pci, NAV_PCI_SIZE);
    print_pci(pci);
    printf("state %d\n", *header_state);
    startcode = find_next_start_code(pb, &size, header_state);
    printf("code %d\n", startcode);
    len = avio_rb16(pb);
    if (startcode != PRIVATE_STREAM_2 ||
        len != NAV_DSI_SIZE) {
        avio_skip(pb, len - 2);
        return;
    }
    avio_read(pb, dsi, NAV_DSI_SIZE);

    vobu->vob_id = dsi[6 * 4] << 8 | dsi[6 * 4 + 1];
    vobu->vob_cell_id = dsi[6 * 4 + 2];

    print_dsi(dsi);
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

            vobus[i].sector = (avio_tell(pb) - 44) / 2048;
            vobus[i].start = avio_tell(pb) - 44;
            if (i) {
                vobus[i - 1].end = vobus[i].start;
            }
           printf("Sector: 0x%08"PRIx32" %"PRId64"\n",
                   vobus[i].sector, vobus[i].start);
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
        if (++i >= size) {
            size *= 2;
            if (av_reallocp_array(&vobus, size, sizeof(VOBU)) < 0)
                return -1;
        }
    }

    vobus[i - 1].end = end;
    vobus[i].sector = end / 2048;
    vobus[i + 1].sector = -1; //FIXME

    *v = vobus;

    avio_close(in);

    return i;
}
