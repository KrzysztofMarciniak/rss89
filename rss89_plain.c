/* rss89_plain [collection [feed]] - print cached articles, newest first.
 * never touches the network. stdout: articles only. errors: stderr. */
#include "rss89.h"

struct entry { const struct item *it; const char *label; };

static struct entry *ents;
static int nents, capents, errors;

static char *xstrdup(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    if (!p) { perror("malloc"); exit(1); }
    return strcpy(p, s);
}

static void load_feed(const char *coll, const char *feed)
{
    char path[PATHMAX], label[520];
    char *buf, *lab;
    struct item *v;
    int n, i;

    sprintf(label, "%s/%s", coll, feed);
    if (!rss_valid_name(coll) || !rss_valid_name(feed) ||
        rss_path(path, "feeds", coll, feed, "feed.xml", (char *)0) != 0 ||
        (buf = rss_slurp(path)) == NULL) {
        fprintf(stderr, "rss89_plain: %s: not cached\n", label);
        errors++;
        return;
    }
    n = rss_parse(buf, &v);
    free(buf);
    lab = xstrdup(label);
    for (i = 0; i < n; i++) {
        if (nents == capents) {
            capents = capents ? capents * 2 : 64;
            ents = realloc(ents, (size_t)capents * sizeof *ents);
            if (!ents) { perror("realloc"); exit(1); }
        }
        ents[nents].it = &v[i];
        ents[nents].label = lab;
        nents++;
    }
}

static void load_coll(const char *coll)
{
    char dir[PATHMAX], file[PATHMAX];
    DIR *d;
    struct dirent *e;

    if (!rss_valid_name(coll) || rss_path(dir, "config", coll, (char *)0) != 0 ||
        (d = opendir(dir)) == NULL) {
        fprintf(stderr, "rss89_plain: %s: no such collection\n", coll);
        errors++;
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (rss_path(file, "config", coll, e->d_name, (char *)0) != 0) continue;
        if (rss_is_file(file)) load_feed(coll, e->d_name);
    }
    closedir(d);
}

static int cmp(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    int c;
    if (x->it->ts != y->it->ts) return x->it->ts < y->it->ts ? 1 : -1;
    c = strcmp(x->label, y->label);
    return c ? c : strcmp(x->it->title, y->it->title);
}

int main(int argc, char **argv)
{
    char dir[PATHMAX], date[40];
    DIR *d;
    struct dirent *e;
    int i;

    if (argc > 3) {
        fprintf(stderr, "usage: rss89_plain [collection [feed]]\n");
        return 2;
    }
    if (argc == 3) {
        load_feed(argv[1], argv[2]);
    } else if (argc == 2) {
        load_coll(argv[1]);
    } else {
        if (rss_path(dir, "config", (char *)0) != 0 || (d = opendir(dir)) == NULL) {
            fprintf(stderr, "rss89_plain: cannot open ~/.rss89/config\n");
            return 1;
        }
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            if (rss_path(dir, "config", e->d_name, (char *)0) == 0 && rss_is_dir(dir))
                load_coll(e->d_name);
        }
        closedir(d);
    }

    qsort(ents, (size_t)nents, sizeof *ents, cmp);
    for (i = 0; i < nents; i++) {
        const struct item *it = ents[i].it;
        if (i) printf("\n");
        rss_date(it->ts, date, sizeof date);
        printf("%s\n%s\n%s\n\n", date, ents[i].label, it->title);
        if (it->desc) printf("%s\n\n", it->desc);
        if (it->link) printf("%s\n\n", it->link);
        printf("---\n");
    }
    return errors ? 1 : 0;
}
