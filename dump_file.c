#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dvdread/dvd_reader.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void help(char *name)
{
    fprintf(stderr, "%s <path> [index] [type] [size] [outfile]\n"
            "path: Any path supported by dvdnav, device, iso or directory\n"
            "index: title index, 0 by default\n"
            "type: file type 0=`ifo`, 1=`menu`, 2=`title`, 3=`bup`,"
            "ifo by default\n"
            "size: print up to size sectors, the whole file by default\n"
            "outfile: write the data to the file",
            name);
    exit(0);
}

static void print_blocks(const uint8_t *buf, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!(i % 8))
            fprintf(stderr, "\n");
        fprintf(stderr, "0x%04x ", buf[i]);
    }
    fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    dvd_reader_t *dvd;
    dvd_file_t *file;
    int i, index = 0, f = -1;
    int size = 0;
    int domain = DVD_READ_INFO_FILE;

    if (argc < 2)
        help(argv[0]);
    if (argc > 2)
        index = atoi(argv[2]);
    if (argc > 3) {
        switch (atoi(argv[3])) {
        case 0:
            domain = DVD_READ_INFO_FILE;
            break;
        case 1:
            domain = DVD_READ_MENU_VOBS;
            break;
        case 2:
            domain = DVD_READ_TITLE_VOBS;
            break;
        case 3:
            domain = DVD_READ_INFO_BACKUP_FILE;
            break;
        default:
            break;
        }
    }
    if (argc > 4)
        size = atoi(argv[4]);

    if (argc > 5)
        f = open(argv[5], O_CREAT|O_RDWR, 0666);

    dvd = DVDOpen(argv[1]);
    if(!dvd) {
        fprintf(stderr, "Cannot open the path %s\n", argv[1]);
        exit(1);
    }

    file = DVDOpenFile(dvd, index, domain);

    if (!size)
        size = DVDFileSize(file);

    for (i = 0; i < size; i++) {
        uint8_t buf[2048];
        DVDReadBlocks(file, i, 1, buf);
        if (f > 0)
            write(f, buf, sizeof(buf));
        else
            print_blocks(buf, sizeof(buf));
    }

    return 0;
}
