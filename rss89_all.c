/* rss_fetch_all - fetch every feed in ~/.rss89/config
 *
 * max 8 simultaneous fetches per host (32 overall). a slot is freed only
 * when that fetch has returned. each fetch is one rss_fetch() call in a
 * child process; swap fork() for pthread_create() in job_start() to use
 * threads, rss_fetch() shares no state.
 * -DRSS89_NO_PARALLEL: no fork/pipe/wait, feeds are fetched one by one.
 */
#include "rss89.h"
#ifndef RSS89_NO_PARALLEL
#include <fcntl.h>
#include <sys/wait.h>
#endif

#define MAXHOST 8
#define MAXTOTAL 32

struct job {
    char coll[256];
    char feed[256];
    char host[256];
    char url[URLMAX];
    pid_t pid;
    int fd;
    int state; /* 0 pending, 1 running, 2 done */
};

struct stats { int updated, unchanged, failed, newitems; };

static struct job *jobs;
static int njobs, capjobs;

static void host_of(const char *url, char *host)
{
    const char *p = strstr(url, "://");
    size_t i = 0;
    p = p ? p + 3 : url;
    while (*p && *p != '/' && *p != ':' && i < 255) host[i++] = *p++;
    host[i] = 0;
}

static void add_job(const char *coll, const char *feed)
{
    struct job *j;
    char file[PATHMAX];

    if (strlen(coll) > 255 || strlen(feed) > 255) return;
    if (njobs == capjobs) {
        capjobs = capjobs ? capjobs * 2 : 16;
        jobs = realloc(jobs, (size_t)capjobs * sizeof *jobs);
        if (!jobs) { perror("realloc"); exit(1); }
    }
    j = &jobs[njobs++];
    strcpy(j->coll, coll);
    strcpy(j->feed, feed);
    j->url[0] = 0;
    if (rss_path(file, "config", coll, feed, (char *)0) == 0) rss_read_url(file, j->url);
    host_of(j->url, j->host);
    j->pid = 0;
    j->fd = -1;
    j->state = 0;
}

static int cmpjob(const void *a, const void *b)
{
    const struct job *x = a, *y = b;
    int c = strcmp(x->coll, y->coll);
    return c ? c : strcmp(x->feed, y->feed);
}

static void scan(void)
{
    char cdir[PATHMAX], sub[PATHMAX], file[PATHMAX];
    DIR *d, *s;
    struct dirent *e, *f;

    if (rss_path(cdir, "config", (char *)0) != 0 || (d = opendir(cdir)) == NULL) {
        fprintf(stderr, "rss89_update: cannot open ~/.rss89/config\n");
        exit(1);
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (rss_path(sub, "config", e->d_name, (char *)0) != 0 || !rss_is_dir(sub)) continue;
        if ((s = opendir(sub)) == NULL) continue;
        while ((f = readdir(s)) != NULL) {
            if (f->d_name[0] == '.') continue;
            if (rss_path(file, "config", e->d_name, f->d_name, (char *)0) != 0) continue;
            if (rss_is_file(file)) add_job(e->d_name, f->d_name);
        }
        closedir(s);
    }
    closedir(d);
    qsort(jobs, (size_t)njobs, sizeof *jobs, cmpjob);
}

static void report(struct job *j, struct fetchres *r, struct stats *s)
{
    char name[520];

    sprintf(name, "%s/%s", j->coll, j->feed);
    j->state = 2;
    if (r->rc == FETCH_OK) {
        s->updated++;
        s->newitems += r->newitems;
        printf("%-10s%-28s%d  %d items\n", "[OK]", name, r->status, r->items);
    } else if (r->rc == FETCH_UNCHANGED) {
        s->unchanged++;
        printf("%-10s%-28s%s\n", "[304]", name, "unchanged");
    } else {
        s->failed++;
        printf("%-10s%-28s%s\n", "[ERROR]", name, r->err);
    }
    fflush(stdout);
}

#ifndef RSS89_NO_PARALLEL
static int job_start(struct job *j)
{
    int fd[2], nul;
    pid_t p;
    struct fetchres r;

    if (pipe(fd) < 0) return -1;
    fflush(stdout);
    p = fork();
    if (p < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (p == 0) {
        close(fd[0]);
        nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); close(nul); }
        rss_fetch(j->coll, j->feed, &r);
        if (write(fd[1], &r, sizeof r) < 0) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    j->fd = fd[0];
    j->pid = p;
    j->state = 1;
    return 0;
}

static void job_finish(struct job *j, struct stats *s)
{
    struct fetchres r;
    int n;

    n = (int)read(j->fd, &r, sizeof r);
    close(j->fd);
    if (n != (int)sizeof r) {
        memset(&r, 0, sizeof r);
        r.rc = FETCH_ERROR;
        strcpy(r.err, "fetch process failed");
    }
    report(j, &r, s);
}

static int host_running(const char *host)
{
    int i, n = 0;
    for (i = 0; i < njobs; i++)
        if (jobs[i].state == 1 && strcmp(jobs[i].host, host) == 0) n++;
    return n;
}

#endif

int rss_fetch_all(void)
{
    struct stats s;
    int i, total;
#ifndef RSS89_NO_PARALLEL
    int running, st;
    pid_t pid;
#endif

    memset(&s, 0, sizeof s);
    printf("rss89: scanning ~/.rss89/config\n\n");
    scan();
    printf("Found %d feeds.\n\nUpdating feeds...\n\n", njobs);
    fflush(stdout);

    total = njobs;
#ifdef RSS89_NO_PARALLEL
    for (i = 0; i < njobs; i++) {
        struct fetchres r;
        rss_fetch(jobs[i].coll, jobs[i].feed, &r);
        report(&jobs[i], &r, &s);
    }
#else
    for (;;) {
        running = 0;
        for (i = 0; i < njobs; i++) if (jobs[i].state == 1) running++;
        for (i = 0; i < njobs && running < MAXTOTAL; i++) {
            if (jobs[i].state == 0 && host_running(jobs[i].host) < MAXHOST) {
                if (job_start(&jobs[i]) < 0) {
                    perror("fork");
                    jobs[i].state = 2;
                    s.failed++;
                } else {
                    running++;
                }
            }
        }
        if (running == 0) break;
        pid = wait(&st);
        if (pid < 0) break;
        for (i = 0; i < njobs; i++)
            if (jobs[i].state == 1 && jobs[i].pid == pid) { job_finish(&jobs[i], &s); break; }
    }
#endif

    printf("\nUpdate complete.\n\n");
    printf("Feeds:     %d\n", total);
    printf("Updated:   %d\n", s.updated);
    printf("Unchanged: %d\n", s.unchanged);
    printf("Failed:    %d\n", s.failed);
    printf("New items: %d\n", s.newitems);
    free(jobs);
    return s.failed ? 1 : 0;
}
