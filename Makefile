CC = cc
# Optional features, e.g. make FEATURES="-DRSS89_USE_CURL -DRSS89_NO_ARCHIVE"
#   -DRSS89_USE_CURL    default fetcher is curl instead of wget
#   -DRSS89_NO_ENV      ignore $RSS89_FETCH
#   -DRSS89_NO_ARCHIVE  no archive snapshots
#   -DRSS89_NO_HDR      no HTTP status / 304 handling
#   -DRSS89_NO_PARALLEL no fork/pipe/wait, fetch feeds one by one
FEATURES =
CFLAGS = -std=c89 -pedantic -Wall -O2 -D_POSIX_C_SOURCE=200112L $(FEATURES)

all: rss89_fetch rss89_plain

rss89.o: rss89.c rss89.h Makefile
	$(CC) $(CFLAGS) -c rss89.c

rss89_all.o: rss89_all.c rss89.h Makefile
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
