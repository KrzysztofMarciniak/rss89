/* rss89_fetch                     - fetch all feeds
 * rss89_fetch <collection> <feed> - fetch one feed, verbose */
#include "rss89.h"

int main(int argc, char **argv)
{
    struct fetchres r;
    char cfg[PATHMAX], url[URLMAX];

    if (argc == 1) return rss_fetch_all();
    if (argc != 3) {
        fprintf(stderr, "usage: rss89_fetch [<collection> <feed>]\n");
        return 2;
    }
    if (!rss_valid_name(argv[1]) || !rss_valid_name(argv[2]) ||
        rss_path(cfg, "config", argv[1], argv[2], (char *)0) != 0 ||
        !rss_read_url(cfg, url)) {
        fprintf(stderr, "rss89_fetch: %s/%s: no such feed\n", argv[1], argv[2]);
        return 1;
    }
    printf("rss89: fetching %s/%s\n\nURL: %s\n\n", argv[1], argv[2], url);
    fflush(stdout);

    rss_fetch(argv[1], argv[2], &r);

    if (r.status) printf("HTTP status: %d\n", r.status);
    if (r.ctype[0]) printf("Content-Type: %s\n", r.ctype);
    if (r.status || r.ctype[0]) printf("\n");

    if (r.rc == FETCH_ERROR) {
        fflush(stdout);
        fprintf(stderr, "rss89_fetch: %s\n", r.err);
        return 1;
    }
    if (r.rc == FETCH_UNCHANGED) {
        printf("Not modified.\n");
        return 0;
    }
    printf("Fetched successfully.\n\n");
    printf("Items: %d\nNew:   %d\n", r.items, r.newitems);
    printf("Saved: ~/.rss89/feeds/%s/%s/feed.xml\n", argv[1], argv[2]);
    if (r.archive[0])
        printf("Archive: ~/.rss89/feeds/%s/%s/archives/%s.xml\n", argv[1], argv[2], r.archive);
    return 0;
}
