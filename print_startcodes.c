#include <stdio.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>

#include <dvdread/nav_print.h>

#include "common.h"

static void help(char *name)
{
    fprintf(stderr, "%s <vob>\n"
            "vob: A VOB file.\n",
            name);
    exit(0);
}

static int print_startcodes(AVIOContext *avio)
{
    int size = MAX_SYNC_SIZE, startcode;
    int32_t header_state = 0xff;

    startcode = find_next_start_code(avio, &size, &header_state);
    if (startcode < 0)
        return AVERROR_EOF;

    fprintf(stderr, "Startcode : 0x%08x pos : 0x%08"PRId64" ",
            startcode, avio_tell(avio));

    switch (startcode) {
    case PACK_START_CODE:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(112),
               "PACK\n");
    break;
    case SYSTEM_HEADER_START_CODE:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(132),
               "SYSTEM\n");
    break;
    case SEQUENCE_END_CODE:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(142),
               "END\n");
    break;
    case PACKET_START_CODE_PREFIX:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(152),
               "PREFIX\n");
    break;
    case ISO_11172_END_CODE:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(121),
               "END2\n");
    break;
    case PRIVATE_STREAM_1:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(124),
               "AUDIO\n");
    break;
    case PRIVATE_STREAM_2: {
        int len = avio_rb16(avio);
        VOBU vobu = { 0 };

        if (len == NAV_PCI_SIZE) {
            parse_nav_pack(avio, &header_state, &vobu);

        }

        av_log(NULL, AV_LOG_INFO|AV_LOG_C(211),
               "NAV %d, vob_id %d cell_id %d\n",
               len, vobu.vob_id, vobu.cell_id);
    }
    break;
    default:
        av_log(NULL, AV_LOG_INFO|AV_LOG_C(222),
               "UNK\n");
    }

    return avio->eof_reached;
}

int main(int argc, char *argv[])
{
    AVIOContext *in = NULL;
    int ret;
    av_register_all();

    if (argc < 2)
        help(argv[0]);

    ret = avio_open(&in, argv[1], AVIO_FLAG_READ);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_ERROR, "Cannot open %s: %s\n",
               argv[1], errbuf);
        return 1;
    }

    while (!print_startcodes(in));

    avio_close(in);

    return 0;
}
