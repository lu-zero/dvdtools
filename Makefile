PKGCONF = pkgconf
PKGCONF_MODULES = dvdread libavformat libavutil
CFLAGS = -Wall -g
CFLAGS += `$(PKGCONF) --cflags $(PKGCONF_MODULES)`
LDFLAGS = `$(PKGCONF) --libs $(PKGCONF_MODULES)`
PROGRAMS = dump_ifo dump_vobu

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS) *.o

dump_ifo: dump_ifo.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


dump_vobu: dump_vobu.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)


