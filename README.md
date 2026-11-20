# rss89

Minimal RSS/Atom reader in C89 (+ POSIX). Three commands, plain files, no libraries.

| Command | Purpose |
| --- | --- |
| `rss89_fetch` | fetch all feeds (max 8 parallel fetches per host) |
| `rss89_fetch <collection> <feed>` | fetch one feed, verbose |
| `rss89_plain [collection [feed]]` | print cached articles, newest first, never uses the network |

## Compile

Needs a C compiler, `make`, a POSIX system and a download tool (curl by default).

    make

## Install

    make install                  # to /usr/local/bin (may need sudo)
    make install PREFIX=$HOME/.local

Uninstall with `make uninstall` (same `PREFIX`).

## Configure

One file per feed: `~/.rss89/config/<collection>/<feed>`, first line is the URL.

    mkdir -p ~/.rss89/config/security
    echo 'https://krebsonsecurity.com/feed' > ~/.rss89/config/security/krebs

Cached data is stored in `~/.rss89/feeds/<collection>/<feed>/feed.xml`.

Archives: every fetch that brings new items also saves a snapshot (UTC) to
`~/.rss89/feeds/<collection>/<feed>/archives/YYYY-MM-DD/HHMMSS.xml`.

## Usage

    rss89_fetch                     # fetch everything
    rss89_fetch security krebs      # fetch one feed
    rss89_plain                     # all cached articles
    rss89_plain security            # one collection
    rss89_plain security krebs      # one feed
    rss89_plain security krebs | less

Example output:

    2026-11-20 09:30 UTC
    security/krebs
    Article title

    Short description.

    https://krebsonsecurity.com/2026/11/article/

    ---

Cron, update every 30 minutes:

    */30 * * * * rss89_fetch >/dev/null 2>&1

## Choosing the download tool

`RSS89_FETCH` is a shell command run for every feed. Default is curl. Variables it receives:

| Variable | Meaning |
| --- | --- |
| `RSS89_URL` | URL to download |
| `RSS89_OUT` | file to write the body to |
| `RSS89_HDR` | file to write response headers to (optional; enables HTTP status, Content-Type, 304) |
| `RSS89_OLD` | previously cached feed (may not exist) |

Exit code 0 means success.

    export RSS89_FETCH='wget -q -O "$RSS89_OUT" "$RSS89_URL"'

Without `RSS89_HDR` the status is assumed to be 200 and 304 is not detected.

## Exit codes

- `rss89_plain`: 1 if a requested feed is not cached
- `rss89_fetch`: 1 on error, 2 on wrong arguments
