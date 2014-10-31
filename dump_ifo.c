#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
#include <dvdread/ifo_print.h>

static void help(char *name)
{
    fprintf(stderr, "%s <path>\n"
            "path: Any path supported by dvdnav, device, iso or directory\n",
            name);
    exit(0);
}

static void dump_cells(c_adt_t *c_adt)
{
    int i, entries;

    entries = (c_adt->last_byte + 1 - C_ADT_SIZE)/sizeof(c_adt_t);

    for(i = 0; i < entries; i++) {
        printf("VOB ID: %3i, Cell ID: %3i   ",
                c_adt->cell_adr_table[i].vob_id,
                c_adt->cell_adr_table[i].cell_id);
        printf("Sector: 0x%08x 0x%08x\n",
           c_adt->cell_adr_table[i].start_sector,
           c_adt->cell_adr_table[i].last_sector);
    }

    printf("Entries: %d\n\n", c_adt->nr_of_vobs);
}

static void dump_vobu_address(vobu_admap_t *vobu_admap) {
  int i, entries;

  entries = (vobu_admap->last_byte + 1 - VOBU_ADMAP_SIZE) / 4;
  for(i = 0; i < entries; i++) {
    printf("Sector: 0x%08x\n",
           vobu_admap->vobu_start_sectors[i]);
  }
}

int main(int argc, char **argv)
{
    dvd_reader_t *dvd;
    ifo_handle_t *ifo;
    int index = 0;

    if (argc < 2)
        help(argv[0]);
    if (argc > 2)
        index = atoi(argv[2]);

    dvd = DVDOpen(argv[1]);
    if(!dvd) {
        fprintf(stderr, "Cannot open the path %s\n", argv[1]);
        exit(1);
    }

    ifo = ifoOpen(dvd, index);

    ifo_print(dvd, index);


    if (ifo->menu_c_adt)
        dump_cells(ifo->menu_c_adt);

    if (ifo->vts_c_adt)
        dump_cells(ifo->vts_c_adt);

    if (ifo->menu_vobu_admap)
        dump_vobu_address(ifo->menu_vobu_admap);

    return 0;
}




