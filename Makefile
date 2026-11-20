CC = cc
CFLAGS = -std=c89 -pedantic -Wall -O2 -D_POSIX_C_SOURCE=200112L

all: rss89_fetch rss89_plain

rss89.o: rss89.c rss89.h
	$(CC) $(CFLAGS) -c rss89.c

rss89_all.o: rss89_all.c rss89.h
	$(CC) $(CFLAGS) -c rss89_all.c

rss89_fetch: rss89_fetch.c rss89.o rss89_all.o rss89.h
	$(CC) $(CFLAGS) -o rss89_fetch rss89_fetch.c rss89.o rss89_all.o

rss89_plain: rss89_plain.c rss89.o rss89.h
	$(CC) $(CFLAGS) -o rss89_plain rss89_plain.c rss89.o

clean:
	rm -f rss89.o rss89_all.o rss89_fetch rss89_plain

PREFIX = /usr/local
BINS = rss89_fetch rss89_plain

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BINS) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	cd $(DESTDIR)$(PREFIX)/bin && rm -f $(BINS)
