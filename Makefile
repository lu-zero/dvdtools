PKGCONF = pkgconf
PKGCONF_MODULES = dvdread libavformat libavutil
CFLAGS = -Wall -g -fsanitize=address
CFLAGS += `$(PKGCONF) --cflags $(PKGCONF_MODULES)`
LDFLAGS = `$(PKGCONF) --libs $(PKGCONF_MODULES)`
PROGRAMS = dump_ifo dump_vobu rewrite_ifo

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS) *.o

dump_ifo: dump_ifo.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


dump_vobu: dump_vobu.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

rewrite_ifo: rewrite_ifo.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


