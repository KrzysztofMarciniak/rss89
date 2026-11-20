/* rss89.h - shared library. C89 + POSIX (dirent, stat, fork, pipe). */
#ifndef RSS89_H
#define RSS89_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define PATHMAX 1024
#define URLMAX 1024

#define FETCH_OK 0
#define FETCH_UNCHANGED 1
#define FETCH_ERROR (-1)

struct item {
    char *title;
    char *link;
    char *key;  /* guid / id / link / title, used to detect new items */
    char *desc; /* plain text */
    time_t ts;  /* UTC, 0 = unknown */
};

struct fetchres {
    int rc;
    int status;
    int items;
    int newitems;
    char ctype[128];
    char err[128];
    char archive[32]; /* "YYYY-MM-DD/HHMMSS", set when a snapshot was archived */
};

/* $HOME/.rss89/a/b/... ; arguments end with (char *)0 */
int rss_path(char *out, ...);
int rss_valid_name(const char *n);
int rss_is_dir(const char *path);
int rss_is_file(const char *path);
int rss_read_url(const char *path, char *url);
char *rss_slurp(const char *path);

int rss_parse(const char *buf, struct item **out);
void rss_free(struct item *v, int n);
void rss_date(time_t t, char *out, size_t n);

/* self-contained, no shared state: safe to run in a process or a thread */
void rss_fetch(const char *coll, const char *feed, struct fetchres *r);

/* fetch every configured feed, 8 per host max, prints progress and summary */
int rss_fetch_all(void);

#endif
