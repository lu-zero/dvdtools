#ifndef COMMON_H
#define COMMON_H

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

#include <dvdread/nav_read.h>

typedef struct {
    int64_t start, end;
    int32_t start_sector, end_sector;
    int32_t next;
    uint16_t vob_id;
    uint8_t  cell_id;
    pci_t pci;
    dsi_t dsi;
} VOBU;

void parse_nav_pack(AVIOContext *pb, int32_t *header_state, VOBU *vobu);
int find_vobu(AVIOContext *pb, VOBU *vobus, int i);
int populate_vobs(VOBU **v, const char *filename);


#endif // COMMON_H
