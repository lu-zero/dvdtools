PKGCONF = pkgconf
PKGCONF_MODULES = dvdread libavformat libavutil
CFLAGS = -Wall -g -fsanitize=address
CFLAGS += `$(PKGCONF) --cflags $(PKGCONF_MODULES)`
LDFLAGS = `$(PKGCONF) --libs $(PKGCONF_MODULES)`
PROGRAMS = dump_ifo dump_file
PROGRAMS += dump_vobu print_vobu
PROGRAMS += rewrite_ifo make_vob
PROGRAMS += print_cell dump_cell
PROGRAMS += print_startcodes

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS) *.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

dump_ifo: dump_ifo.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

dump_file: dump_file.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

make_vob: make_vob.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

print_vobu: print_vobu.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

dump_vobu: dump_vobu.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

print_cell: print_cell.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

dump_cell: dump_cell.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

rewrite_ifo: rewrite_ifo.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

print_startcodes: print_startcodes.c common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


