#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)

#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf

#define NAV_PCI_SIZE 980
#define NAV_DSI_SIZE 1018

#define NAV_PACK_SIZE NAV_PCI_SIZE + NAV_DSI_SIZE

#define MAX_SYNC_SIZE 100000

static void help(char *name)
{
    fprintf(stderr, "%s <vob>\n"
            "vob: A VOB file.\n",
            name);
    exit(0);
}

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

static void parse_nav_pack(AVIOContext *pb, int32_t *header_state)
{
    int size, startcode, len;
    uint8_t pci[NAV_PCI_SIZE];
    uint8_t dsi[NAV_DSI_SIZE];

    avio_read(pb, pci, NAV_PCI_SIZE);
    startcode = find_next_start_code(pb, &size, header_state);
    len = avio_rb16(pb);
    if (startcode != PRIVATE_STREAM_2 ||
        len != NAV_DSI_SIZE) {
        avio_skip(pb, size - 2);
        return;
    }
    avio_read(pb, dsi, NAV_DSI_SIZE);

    print_pci(pci);
    print_dsi(dsi);
}

static int find_vobu(AVIOContext *pb)
{
    int size, startcode;
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
            printf("Sector: 0x%08"PRIx64"\n",
                   (avio_tell(pb) - 44) / 2048);

            parse_nav_pack(pb, &header_state);
            return 0;
         } else {
            avio_skip(pb, len);
            goto redo;
        }
    } else {
        goto redo;
    }
}

int main(int argc, char *argv[])
{
    AVIOContext *pb = NULL;
    int ret;

    av_register_all();

    if (argc < 2)
        help(argv[0]);

    ret = avio_open(&pb, argv[1], AVIO_FLAG_READ);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_ERROR, "Cannot open %s: %s",
               argv[1], errbuf);
        return 1;
    }

    while (!find_vobu(pb));

    return 0;
}