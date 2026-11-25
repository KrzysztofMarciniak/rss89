/* rss89.c - paths, xml parsing, fetching.
 *
 * Fetching runs the shell command in $RSS89_FETCH. The command sees:
 *   RSS89_URL  url to download
 *   RSS89_OUT  file to write the body to
 *   RSS89_HDR  file to write response headers to (optional, enables
 *              HTTP status, Content-Type and 304 handling)
 *   RSS89_OLD  previously cached feed (may not exist)
 * exit 0 = success. Default is curl. wget example (no headers):
 *   RSS89_FETCH='wget -q -O "$RSS89_OUT" "$RSS89_URL"'
 */
#include "rss89.h"
#include <stdarg.h>
#include <sys/wait.h>

#ifdef RSS89_USE_CURL
# ifdef RSS89_NO_HDR
#  define DEFAULT_FETCH "curl -sSL --max-time 30 -o \"$RSS89_OUT\" \"$RSS89_URL\""
# else
#  define DEFAULT_FETCH \
    "if [ -f \"$RSS89_OLD\" ]; then " \
    "curl -sSL --max-time 30 -z \"$RSS89_OLD\" -D \"$RSS89_HDR\" -o \"$RSS89_OUT\" \"$RSS89_URL\"; " \
    "else " \
    "curl -sSL --max-time 30 -D \"$RSS89_HDR\" -o \"$RSS89_OUT\" \"$RSS89_URL\"; fi"
# endif
#else
/* wget: -S puts the response headers in the -o log (indented, parse_hdr
 * skips the indent). wget exits 8 on a 304, so that case is mapped to 0. */
# ifdef RSS89_NO_HDR
#  define DEFAULT_FETCH "wget -q -T 30 -O \"$RSS89_OUT\" \"$RSS89_URL\""
# else
#  define DEFAULT_FETCH \
    "ims=; " \
    "if [ -f \"$RSS89_OLD\" ]; then " \
    "ims=\"If-Modified-Since: $(LC_ALL=C date -u -r \"$RSS89_OLD\" '+%a, %d %b %Y %H:%M:%S GMT')\"; fi; " \
    "wget -S -T 30 ${ims:+--header=\"$ims\"} -o \"$RSS89_HDR\" -O \"$RSS89_OUT\" \"$RSS89_URL\"; rc=$?; " \
    "if [ $rc -eq 8 ] && grep -q 'HTTP/[0-9.]* 304' \"$RSS89_HDR\"; then exit 0; fi; exit $rc"
# endif
#endif

static char *xdup(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p) { perror("malloc"); exit(1); }
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ---------- paths and files ---------- */

int rss_path(char *out, ...)
{
    va_list ap;
    const char *home = getenv("HOME"), *a;
    size_t len;

    if (!home) home = ".";
    len = strlen(home) + 7;
    if (len >= PATHMAX) return -1;
    strcpy(out, home);
    strcat(out, "/.rss89");
    va_start(ap, out);
    while ((a = va_arg(ap, const char *)) != NULL) {
        len += strlen(a) + 1;
        if (len >= PATHMAX) { va_end(ap); return -1; }
        strcat(out, "/");
        strcat(out, a);
    }
    va_end(ap);
    return 0;
}

int rss_valid_name(const char *n)
{
    return n && n[0] && n[0] != '.' && !strchr(n, '/') && strlen(n) < 256;
}

int rss_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int rss_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int rss_read_url(const char *path, char *url)
{
    FILE *f;
    size_t l;
    url[0] = 0;
    f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(url, URLMAX, f)) { url[0] = 0; fclose(f); return 0; }
    fclose(f);
    l = strlen(url);
    while (l > 0 && isspace((unsigned char)url[l - 1])) url[--l] = 0;
    return l > 0;
}

char *rss_slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) { fclose(f); return NULL; }
    rewind(f);
    b = malloc((size_t)n + 1);
    if (!b) { perror("malloc"); exit(1); }
    n = (long)fread(b, 1, (size_t)n, f);
    b[n] = 0;
    fclose(f);
    return b;
}

static void mkdirs(const char *coll, const char *feed)
{
    char p[PATHMAX];
    if (rss_path(p, (char *)0) == 0) mkdir(p, 0755);
    if (rss_path(p, "feeds", (char *)0) == 0) mkdir(p, 0755);
    if (rss_path(p, "feeds", coll, (char *)0) == 0) mkdir(p, 0755);
    if (rss_path(p, "feeds", coll, feed, (char *)0) == 0) mkdir(p, 0755);
}

/* ---------- text cleanup ---------- */

static void squeeze(char *s)
{
    char *r = s, *w = s;
    int sp = 1;
    for (; *r; r++) {
        if (isspace((unsigned char)*r)) {
            if (!sp) { *w++ = ' '; sp = 1; }
        } else { *w++ = *r; sp = 0; }
    }
    if (w > s && w[-1] == ' ') w--;
    *w = 0;
}

static int utf8(char *w, unsigned long c)
{
    if (c < 0x80) { w[0] = (char)c; return 1; }
    if (c < 0x800) {
        w[0] = (char)(0xC0 | (c >> 6));
        w[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000UL) {
        w[0] = (char)(0xE0 | (c >> 12));
        w[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        w[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    w[0] = (char)(0xF0 | (c >> 18));
    w[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    w[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    w[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static const struct { const char *name; const char *rep; } ents[] = {
    { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
    { "&apos;", "'" }, { "&nbsp;", " " }, { "&mdash;", "\xE2\x80\x94" },
    { "&ndash;", "\xE2\x80\x93" }, { "&hellip;", "\xE2\x80\xA6" },
    { "&lsquo;", "\xE2\x80\x98" }, { "&rsquo;", "\xE2\x80\x99" },
    { "&ldquo;", "\xE2\x80\x9C" }, { "&rdquo;", "\xE2\x80\x9D" }
};

static void decode(char *s)
{
    char *r = s, *w = s, *e, *st;
    unsigned long c;
    size_t i, l;
    while (*r) {
        if (*r == '&') {
            if (r[1] == '#') {
                st = (r[2] == 'x' || r[2] == 'X') ? r + 3 : r + 2;
                c = strtoul(st, &e, (st == r + 3) ? 16 : 10);
                if (e > st && *e == ';' && c > 0 && c < 0x110000UL) {
                    w += utf8(w, c);
                    r = e + 1;
                    continue;
                }
            } else {
                for (i = 0; i < sizeof ents / sizeof ents[0]; i++) {
                    l = strlen(ents[i].name);
                    if (!strncmp(r, ents[i].name, l)) {
                        strcpy(w, ents[i].rep);
                        w += strlen(ents[i].rep);
                        r += l;
                        break;
                    }
                }
                if (i < sizeof ents / sizeof ents[0]) continue;
            }
        }
        *w++ = *r++;
    }
    *w = 0;
}

static int block_tag(const char *t)
{
    char c, d;
    if (*t == '/') t++;
    c = (char)tolower((unsigned char)t[0]);
    d = (char)tolower((unsigned char)t[1]);
    return (c == 'p' && !isalpha((unsigned char)d)) ||
           (c == 'b' && d == 'r') || (c == 'd' && d == 'i') ||
           (c == 'l' && d == 'i') || (c == 'h' && isdigit((unsigned char)d)) ||
           (c == 't' && d == 'r');
}

static void strip_tags(char *s)
{
    char *r = s, *w = s;
    int in = 0;
    for (; *r; r++) {
        if (in) {
            if (*r == '>') in = 0;
        } else if (*r == '<' && (isalpha((unsigned char)r[1]) || r[1] == '/' ||
                                 r[1] == '!' || r[1] == '?')) {
            in = 1;
            if (block_tag(r + 1)) *w++ = ' ';
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
}

static void finish(char *s, int html, size_t max)
{
    size_t l;
    squeeze(s);
    if (!strncmp(s, "<![CDATA[", 9)) {
        memmove(s, s + 9, strlen(s + 9) + 1);
        l = strlen(s);
        if (l >= 3 && !strcmp(s + l - 3, "]]>")) s[l - 3] = 0;
    }
    decode(s);
    if (html) strip_tags(s);
    squeeze(s);
    l = strlen(s);
    if (max > 3 && l > max) {
        l = max - 3;
        while (l > 0 && ((unsigned char)s[l] & 0xC0) == 0x80) l--;
        strcpy(s + l, "...");
    }
}

/* ---------- xml ---------- */

/* "<tag" followed by a delimiter */
static const char *find_open(const char *p, const char *tag)
{
    char open[48];
    size_t l;
    char c;
    sprintf(open, "<%s", tag);
    l = strlen(open);
    while ((p = strstr(p, open)) != NULL) {
        c = p[l];
        if (c == '>' || c == '/' || isspace((unsigned char)c)) return p;
        p++;
    }
    return NULL;
}

static char *extract(const char *s, const char *tag, size_t max, int html)
{
    const char *p = find_open(s, tag), *q, *r;
    char close[48], *out;
    if (!p) return NULL;
    q = strchr(p, '>');
    if (!q || q[-1] == '/') return NULL;
    sprintf(close, "</%s>", tag);
    r = strstr(q + 1, close);
    if (!r) return NULL;
    out = xdup(q + 1, (size_t)(r - (q + 1)));
    finish(out, html, max);
    if (!out[0]) { free(out); return NULL; }
    return out;
}

static char *href_copy(const char *h)
{
    const char *r = strchr(h, '"');
    char *out;
    if (!r) return NULL;
    out = xdup(h, (size_t)(r - h));
    decode(out);
    return out;
}

/* rss: <link>text</link>   atom: <link rel="alternate" href="..."/> */
static char *extract_link(const char *s)
{
    const char *p = s, *q, *h, *rel, *r, *fallback = NULL;
    char *out;
    while ((p = find_open(p, "link")) != NULL) {
        q = strchr(p, '>');
        if (!q) break;
        h = strstr(p, "href=\"");
        if (h && h < q) {
            rel = strstr(p, "rel=\"");
            if (rel && rel < q && strncmp(rel + 5, "alternate", 9) != 0) {
                if (!fallback) fallback = h + 6;
            } else {
                return href_copy(h + 6);
            }
        } else if (q[-1] != '/') {
            r = strstr(q + 1, "</link>");
            if (r) {
                out = xdup(q + 1, (size_t)(r - (q + 1)));
                finish(out, 0, 1024);
                if (out[0]) return out;
                free(out);
            }
        }
        p = q;
    }
    return fallback ? href_copy(fallback) : NULL;
}

/* ---------- dates ---------- */

static long days_from_civil(long y, long m, long d)
{
    long era, yoe, doy, doe;
    if (m <= 2) y--;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L;
}

static int month_of(const char *s)
{
    static const char *mn = "janfebmaraprmayjunjulaugsepoctnovdec";
    int i;
    for (i = 0; i < 12; i++)
        if (tolower((unsigned char)s[0]) == mn[i * 3] &&
            tolower((unsigned char)s[1]) == mn[i * 3 + 1] &&
            tolower((unsigned char)s[2]) == mn[i * 3 + 2])
            return i + 1;
    return 0;
}

/* minutes east of UTC */
static long zone_offset(const char *z)
{
    static const struct { const char *n; long m; } zn[] = {
        { "EST", -300 }, { "EDT", -240 }, { "CST", -360 }, { "CDT", -300 },
        { "MST", -420 }, { "MDT", -360 }, { "PST", -480 }, { "PDT", -420 }
    };
    char *e;
    long v, hh, mm, sign;
    size_t i;
    while (isspace((unsigned char)*z)) z++;
    if (*z == '+' || *z == '-') {
        sign = (*z == '-') ? -1 : 1;
        v = strtol(z + 1, &e, 10);
        if (*e == ':') { hh = v; mm = strtol(e + 1, NULL, 10); }
        else if (e - (z + 1) <= 2) { hh = v; mm = 0; }
        else { hh = v / 100; mm = v % 100; }
        return sign * (hh * 60 + mm);
    }
    for (i = 0; i < sizeof zn / sizeof zn[0]; i++)
        if (!strncmp(z, zn[i].n, 3)) return zn[i].m;
    return 0;
}

static time_t parse_date(const char *s)
{
    long y, mo, d, h = 0, mi = 0, sec = 0, off = 0;
    char *e;
    while (isspace((unsigned char)*s)) s++;
    if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
        isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) && s[4] == '-') {
        y = strtol(s, &e, 10);
        if (*e != '-') return 0;
        mo = strtol(e + 1, &e, 10);
        if (*e != '-') return 0;
        d = strtol(e + 1, &e, 10);
        if (*e == 'T' || *e == ' ') {
            h = strtol(e + 1, &e, 10);
            if (*e == ':') {
                mi = strtol(e + 1, &e, 10);
                if (*e == ':') {
                    sec = strtol(e + 1, &e, 10);
                    if (*e == '.') { e++; while (isdigit((unsigned char)*e)) e++; }
                }
            }
            off = zone_offset(e);
        }
    } else {
        while (*s && !isdigit((unsigned char)*s)) s++;
        d = strtol(s, &e, 10);
        s = e;
        while (isspace((unsigned char)*s) || *s == '-') s++;
        mo = month_of(s);
        if (!mo) return 0;
        while (isalpha((unsigned char)*s)) s++;
        while (isspace((unsigned char)*s) || *s == '-') s++;
        y = strtol(s, &e, 10);
        if (y < 100) y += (y < 70) ? 2000 : 1900;
        s = e;
        while (isspace((unsigned char)*s)) s++;
        if (isdigit((unsigned char)*s)) {
            h = strtol(s, &e, 10);
            if (*e == ':') {
                mi = strtol(e + 1, &e, 10);
                if (*e == ':') sec = strtol(e + 1, &e, 10);
            }
            off = zone_offset(e);
        }
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return (time_t)days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - off * 60;
}

/* sciencedirect has no pubDate, only "Publication date: March 2027" in the
 * description. accepts "Month YYYY" and "D Month YYYY". */
static time_t desc_date(const char *desc)
{
    const char *p = strstr(desc, "Publication date:");
    char *e;
    int mo;
    long y;
    if (!p) return 0;
    p += 17;
    while (isspace((unsigned char)*p)) p++;
    if (isdigit((unsigned char)*p)) return parse_date(p);
    mo = month_of(p);
    if (!mo) return 0;
    while (isalpha((unsigned char)*p)) p++;
    y = strtol(p, &e, 10);
    if (e == p || y < 1970 || y > 2999) return 0;
    return (time_t)days_from_civil(y, mo, 1) * 86400;
}

void rss_date(time_t t, char *out, size_t n)
{
    struct tm *g = t ? gmtime(&t) : NULL;
    if (!g || strftime(out, n, "%Y-%m-%d %H:%M UTC", g) == 0)
        strcpy(out, "unknown date");
}

/* ---------- items ---------- */

static void parse_item(const char *blk, struct item *it)
{
    static const char *dtags[] = { "pubDate", "updated", "published", "dc:date" };
    static const char *xtags[] = { "description", "summary", "content:encoded", "content" };
    char *d;
    int i;

    it->title = extract(blk, "title", 512, 1);
    if (!it->title) it->title = xdup("(untitled)", 10);
    it->link = extract_link(blk);
    it->desc = NULL;
    for (i = 0; i < 4 && !it->desc; i++) it->desc = extract(blk, xtags[i], 800, 1);
    it->key = extract(blk, "guid", 512, 0);
    if (!it->key) it->key = extract(blk, "id", 512, 0);
    if (!it->key && it->link) it->key = xdup(it->link, strlen(it->link));
    if (!it->key) it->key = xdup(it->title, strlen(it->title));
    it->ts = 0;
    for (i = 0; i < 4 && !it->ts; i++) {
        d = extract(blk, dtags[i], 128, 0);
        if (d) { it->ts = parse_date(d); free(d); }
    }
    if (!it->ts && it->desc) it->ts = desc_date(it->desc);
}

int rss_parse(const char *buf, struct item **out)
{
    const char *open = "item", *close = "</item>", *p, *e, *next;
    struct item *v = NULL;
    int n = 0, cap = 0;
    char *blk;

    if (!find_open(buf, "item")) { open = "entry"; close = "</entry>"; }
    p = find_open(buf, open);
    while (p) {
        e = strstr(p, close);
        next = e ? find_open(e, open) : NULL;
        blk = xdup(p, e ? (size_t)(e - p) : strlen(p));
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            v = realloc(v, (size_t)cap * sizeof *v);
            if (!v) { perror("realloc"); exit(1); }
        }
        parse_item(blk, &v[n++]);
        free(blk);
        p = next;
    }
    *out = v;
    return n;
}

void rss_free(struct item *v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        free(v[i].title);
        free(v[i].link);
        free(v[i].key);
        free(v[i].desc);
    }
    free(v);
}

/* ---------- fetching ---------- */

#ifndef RSS89_NO_ARCHIVE
static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[8192];
    size_t n;
    int ok = 1;
    if (!a) return -1;
    b = fopen(to, "wb");
    if (!b) { fclose(a); return -1; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n) { ok = 0; break; }
    fclose(a);
    if (fclose(b) != 0) ok = 0;
    return ok ? 0 : -1;
}
/* feeds/<coll>/<feed>/archives/YYYY-MM-DD/HHMMSS.xml (UTC) */
static void archive(const char *coll, const char *feed, const char *cur,
                    struct fetchres *r)
{
    char p[PATHMAX], day[16], tm[16];
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    if (!g) return;
    strftime(day, sizeof day, "%Y-%m-%d", g);
    strftime(tm, sizeof tm, "%H%M%S", g);
    if (rss_path(p, "feeds", coll, feed, "archives", (char *)0) != 0) return;
    mkdir(p, 0755);
    if (rss_path(p, "feeds", coll, feed, "archives", day, (char *)0) != 0) return;
    mkdir(p, 0755);
    if (rss_path(p, "feeds", coll, feed, "archives", day, tm, (char *)0) != 0) return;
    if (strlen(p) + 5 >= PATHMAX) return;
    strcat(p, ".xml");
    if (copy_file(cur, p) == 0) sprintf(r->archive, "%s/%s", day, tm);
}
#endif

static void seterr(struct fetchres *r, const char *msg)
{
    r->rc = FETCH_ERROR;
    strncpy(r->err, msg, sizeof r->err - 1);
    r->err[sizeof r->err - 1] = 0;
}

static int ci_prefix(const char *s, const char *p)
{
    while (*p) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return 0;
        s++;
        p++;
    }
    return 1;
}

static char *shq(const char *s)
{
    size_t n = 3;
    const char *p;
    char *o, *w;
    for (p = s; *p; p++) n += (*p == '\'') ? 4 : 1;
    o = w = malloc(n);
    if (!o) { perror("malloc"); exit(1); }
    *w++ = '\'';
    for (p = s; *p; p++) {
        if (*p == '\'') { memcpy(w, "'\\''", 4); w += 4; }
        else *w++ = *p;
    }
    *w++ = '\'';
    *w = 0;
    return o;
}

/* last HTTP status line and its Content-Type (redirects append blocks) */
static void parse_hdr(const char *path, int *status, char *ctype, size_t n)
{
    char *b = rss_slurp(path), *p, *e, *q;
    size_t l;
    *status = 0;
    ctype[0] = 0;
    if (!b) return;
    for (p = b; *p; p = e) {
        e = strchr(p, '\n');
        if (e) *e++ = 0; else e = p + strlen(p);
        l = strlen(p);
        if (l && p[l - 1] == '\r') p[l - 1] = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (ci_prefix(p, "HTTP/")) {
            q = strchr(p, ' ');
            *status = q ? atoi(q + 1) : 0;
            ctype[0] = 0;
        } else if (ci_prefix(p, "content-type:")) {
            q = p + 13;
            while (isspace((unsigned char)*q)) q++;
            strncpy(ctype, q, n - 1);
            ctype[n - 1] = 0;
        }
    }
    free(b);
}

void rss_fetch(const char *coll, const char *feed, struct fetchres *r)
{
    char cfg[PATHMAX], url[URLMAX], cur[PATHMAX], tmp[PATHMAX], hdr[PATHMAX];
    char msg[128], *u, *o, *h, *old, *c, *cmd, *body;
    const char *fc;
    struct item *nv = NULL, *ov = NULL;
    int ret, code, nn, on = 0, i, j, found;

    memset(r, 0, sizeof *r);
    r->rc = FETCH_ERROR;
    if (!rss_valid_name(coll) || !rss_valid_name(feed)) { seterr(r, "invalid name"); return; }
    if (rss_path(cfg, "config", coll, feed, (char *)0) != 0 || !rss_read_url(cfg, url)) {
        seterr(r, "missing or empty config");
        return;
    }
    mkdirs(coll, feed);
    if (rss_path(cur, "feeds", coll, feed, "feed.xml", (char *)0) != 0 ||
        rss_path(tmp, "feeds", coll, feed, "feed.xml.tmp", (char *)0) != 0 ||
        rss_path(hdr, "feeds", coll, feed, "feed.hdr.tmp", (char *)0) != 0) {
        seterr(r, "path too long");
        return;
    }
    remove(tmp);
    remove(hdr);

#ifdef RSS89_NO_ENV
    fc = DEFAULT_FETCH;
#else
    fc = getenv("RSS89_FETCH");
    if (!fc || !*fc) fc = DEFAULT_FETCH;
#endif
    u = shq(url); o = shq(tmp); h = shq(hdr); old = shq(cur); c = shq(fc);
    cmd = malloc(strlen(u) + strlen(o) + strlen(h) + strlen(old) + strlen(c) + 80);
    if (!cmd) { perror("malloc"); exit(1); }
    sprintf(cmd, "RSS89_URL=%s RSS89_OUT=%s RSS89_HDR=%s RSS89_OLD=%s sh -c %s", u, o, h, old, c);
    ret = system(cmd);
    free(u); free(o); free(h); free(old); free(c); free(cmd);

    if (ret != 0) {
        code = (ret != -1 && WIFEXITED(ret)) ? WEXITSTATUS(ret) : -1;
        if (ret == -1) strcpy(msg, "cannot run fetch command");
#ifdef RSS89_USE_CURL
        else if (code == 6) strcpy(msg, "cannot resolve host");
        else if (code == 7) strcpy(msg, "connection refused");
        else if (code == 28) strcpy(msg, "connection timeout");
#else
        else if (code == 4) strcpy(msg, "network failure");
        else if (code == 5) strcpy(msg, "SSL failure");
        else if (code == 8) strcpy(msg, "server error response");
#endif
        else sprintf(msg, "fetch command failed (exit %d)", code);
        remove(tmp);
        remove(hdr);
        seterr(r, msg);
        return;
    }

    parse_hdr(hdr, &r->status, r->ctype, sizeof r->ctype);
    remove(hdr);
    if (r->status == 304) { remove(tmp); r->rc = FETCH_UNCHANGED; return; }
    if (r->status >= 300) {
        sprintf(msg, "HTTP %d", r->status);
        remove(tmp);
        seterr(r, msg);
        return;
    }
    if (r->status == 0) r->status = 200;

    body = rss_slurp(tmp);
    if (!body || !*body) { free(body); remove(tmp); seterr(r, "empty response"); return; }
    if (!strstr(body, "<rss") && !strstr(body, "<feed") && !strstr(body, "<rdf:RDF")) {
        free(body);
        remove(tmp);
        seterr(r, "not a feed");
        return;
    }
    nn = rss_parse(body, &nv);
    free(body);
    body = rss_slurp(cur);
    if (body) { on = rss_parse(body, &ov); free(body); }

    r->items = nn;
    for (i = 0; i < nn; i++) {
        found = 0;
        for (j = 0; j < on && !found; j++)
            if (strcmp(nv[i].key, ov[j].key) == 0) found = 1;
        if (!found) r->newitems++;
    }
    rss_free(nv, nn);
    rss_free(ov, on);

    if (rename(tmp, cur) != 0) { remove(tmp); seterr(r, "cannot save"); return; }
    r->rc = FETCH_OK;
#ifndef RSS89_NO_ARCHIVE
    if (r->newitems > 0) archive(coll, feed, cur, r);
#endif
}
