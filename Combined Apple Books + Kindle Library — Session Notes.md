# Combined Apple Books + Kindle Library — Session Notes

Oct 4, 2026 · @Dimitry

## Goal and approach

Build one combined list of all Apple Books and Kindle books (title, author, language, year, publisher, source) by reading both apps' local SQLite databases on the Mac. No existing app does this automatically.

- Both databases are plain SQLite (header `SQLite format 3`). The "file is encrypted or is not a database" error was SQLite's catch-all for a locked/inconsistent copy, not real encryption.
- Always open read-only and immutable, so nothing fights the apps for locks: `sqlite3 "file:<path>?mode=ro&immutable=1"`. SQLite does not expand `~`; use `$HOME` in the shell or full paths inside sqlite3.
- Kindle author fields ARE encrypted (deterministic AES), so Kindle authors come from a hand-filled mapping table plus title lookups.
- Export each side with `.mode json`, then merge in a script.

## Apple Books

Database: `~/Library/Containers/com.apple.iBooksX/Data/Documents/BKLibrary/BKLibrary-1-091020131601.sqlite` (suffix varies; use `BKLibrary-*.sqlite`). Main table: `ZBKLIBRARYASSET`.

| Column | Meaning |
| --- | --- |
| `ZTITLE`, `ZAUTHOR` | Title, author (plain text) |
| `ZLANGUAGE` | Language from EPUB metadata (`ru`, `rus`, `ru-RU`, or empty) |
| `ZYEAR` | Year |
| `ZPURCHASEDDSID` | Apple ID that bought it; NULL = book added by me |
| `ZPURCHASEDATE` | Core Data timestamp: add `978307200` for Unix time |
| `ZISSAMPLE`, `ZISSTOREAUDIOBOOK`, `ZISHIDDEN` | Filters |
| `ZEPUBID` | Own EPUB identifier, sometimes an ISBN |

No publisher column, so the query emits `NULL AS publisher` to keep the same columns as Kindle.

```sql
SELECT ZTITLE AS title, ZAUTHOR AS author, ZLANGUAGE AS language,
       ZYEAR AS year, NULL AS publisher,
       CASE WHEN ZPURCHASEDDSID IS NULL THEN 'Apple (added)' ELSE 'Apple (purchased)' END AS source
FROM ZBKLIBRARYASSET
WHERE COALESCE(ZISSAMPLE,0) = 0
  AND COALESCE(ZISSTOREAUDIOBOOK,0) = 0
  AND COALESCE(ZISHIDDEN,0) = 0
  AND ZTITLE IS NOT NULL
ORDER BY author, title;
```

Export: `.mode json`, `.once apple_books.json`, then the query. (`.mode csv --title on` for CSV.)

## Kindle

Database (current Mac App Store app): `~/Library/Containers/com.amazon.Lassen/Data/Library/Protected/BookData.sqlite`. Main table: `ZBOOK`; series in `ZGROUP` + `ZGROUPITEM`.

| `ZRAWBOOKTYPE` | Count | Meaning |
| --- | --- | --- |
| 10 | 288 | Kindle store purchases (ASIN `B0…`) |
| 13 | 269 | Personal docs I uploaded (GUID ids) |
| 16 | 42 | Dictionaries (excluded) |
| 17 | 17 | Unclear: KU / Prime Reading / loans / samples? |
| 19 | 2 | Unclear |

| Column | Meaning |
| --- | --- |
| `ZDISPLAYTITLE` | Title (plain text) |
| `ZLANGUAGE` | Language |
| `ZRAWPUBLICATIONDATE` | Unix seconds (0 = unknown) |
| `ZRAWPUBLISHER` | Publisher (VARCHAR; check it is readable) |
| `ZBOOKID` | `A:<ASIN>-0`; some ASINs are ISBN-10 |
| `ZDISPLAYAUTHOR`, `ZSORTAUTHOR`, `ZALTERNATESORTAUTHOR` | Author: encrypted BLOB |
| `ZGROUP.ZDISPLAYAUTHOR` | Series author, plain text |

**Why authors are encrypted, not encoded:** bytes are outside ASCII (not base64), no zlib/`bplist00` header, and lengths come in exact 16-byte steps that grow with the name (16, 32, 48, 64). That is AES-style block encryption. It is deterministic: the same name always gives the same bytes, so `hex(ZSORTAUTHOR)` works as an author key. Different spellings give different keys (Lindsey Davis appears under two).

```sql
SELECT b.ZDISPLAYTITLE AS title,
       COALESCE(a.author, g.ZDISPLAYAUTHOR) AS author,
       b.ZLANGUAGE AS language,
       CASE WHEN b.ZRAWPUBLICATIONDATE > 0
            THEN strftime('%Y', b.ZRAWPUBLICATIONDATE, 'unixepoch') END AS year,
       b.ZRAWPUBLISHER AS publisher,
       CASE b.ZRAWBOOKTYPE WHEN 10 THEN 'Kindle (purchased)'
                           WHEN 13 THEN 'Kindle (personal doc)'
                           ELSE 'Kindle (other)' END AS source
FROM ZBOOK b
LEFT JOIN m.author_map a ON a.author_key = hex(b.ZSORTAUTHOR)
LEFT JOIN ZGROUPITEM gi ON gi.ZBOOK = b.Z_PK
LEFT JOIN ZGROUP g ON g.Z_PK = gi.ZPARENTCONTAINER AND g.Z_ENT = gi.Z7_PARENTCONTAINER
WHERE b.ZRAWBOOKTYPE <> 16
  AND COALESCE(b.ZRAWISDICTIONARY,0) = 0
  AND COALESCE(b.ZRAWISHIDDEN,0) = 0
  AND COALESCE(b.ZISHIDDENBYUSER,0) = 0
GROUP BY b.Z_PK
ORDER BY author, title;
```

Needs the author map attached as `m` (next section). `GROUP BY b.Z_PK` stops a book in both a series and a collection appearing twice.

## Author mapping table

A separate writable database maps each encrypted author key to a real name. `ATTACH` opens it alongside the read-only `BookData.sqlite` as `m`; re-run `ATTACH` every session. Keep it in a permanent folder (not `/tmp`) and create the folder first.

1. Create the mapping database and table:

```sql
ATTACH '<path_to_authors_db>/kindle_authors.db' AS m;
CREATE TABLE IF NOT EXISTS m.author_map (
    author_key TEXT PRIMARY KEY,
    author     TEXT
);
```

2. Generate INSERT lines for keys not mapped yet (re-runs only add new authors, never overwrite filled names):

```sql
.mode list
.headers off
.once <path_to_authors_db>/author_map_new.sql
SELECT 'INSERT OR REPLACE INTO m.author_map VALUES (''' || hex(b.ZSORTAUTHOR) || ''', '''');  -- '
       || COUNT(*) || ' books: ' || substr(group_concat(b.ZDISPLAYTITLE, ' | '), 1, 100)
FROM ZBOOK b
WHERE b.ZRAWBOOKTYPE <> 16
  AND b.ZSORTAUTHOR IS NOT NULL
  AND hex(b.ZSORTAUTHOR) NOT IN (SELECT author_key FROM m.author_map)
GROUP BY hex(b.ZSORTAUTHOR)
ORDER BY COUNT(*) DESC;
```

3. Edit the file: type the name between the empty quotes, delete unwanted lines. The key must be the full hex string (the file already has it). Escape apostrophes by doubling: `'Patrick O''Brian'`.

```sql
INSERT OR REPLACE INTO m.author_map VALUES ('<full author key>', 'James Hadley Chase');
```

4. Load it and check:

```sql
.read <path_to_authors_db>/author_map_new.sql
SELECT COUNT(*) FROM m.author_map;
```

Known keys (prefixes): `51802E97…` James Hadley Chase (62), `3FF6F826…` Clifford D. Simak (26), `18C57BE3…` Ellis Peters (21), `B513D4B2…` and `ADF7346B…` Lindsey Davis (19 + 8).

## Finding authors by title

Only one lookup per author key is needed, not one per book: resolve one title and the name covers the whole group. Neither database has an ISBN column; `ZBOOKID` holds ASINs, and `ZEPUBID` (Apple) is sometimes an ISBN.

One title per key, picking the longest (most distinctive) title:

```sql
SELECT author_key, title, language, books FROM (
  SELECT hex(ZSORTAUTHOR) AS author_key,
         ZDISPLAYTITLE AS title,
         ZLANGUAGE AS language,
         COUNT(*) OVER (PARTITION BY hex(ZSORTAUTHOR)) AS books,
         ROW_NUMBER() OVER (PARTITION BY hex(ZSORTAUTHOR)
                            ORDER BY length(ZDISPLAYTITLE) DESC) AS rn
  FROM ZBOOK
  WHERE ZRAWBOOKTYPE <> 16 AND ZSORTAUTHOR IS NOT NULL
)
WHERE rn = 1
ORDER BY books DESC;
```

If the longest title is a filename-style upload (e.g. `.fb2` names) and the lookup fails, retry with `rn = 2`.

| Source | Request | Author field |
| --- | --- | --- |
| Open Library | `https://openlibrary.org/search.json?title=<title>&fields=title,author_name&limit=3` | `author_name` |
| Google Books | `https://www.googleapis.com/books/v1/volumes?q=intitle:<title>` (+ `&langRestrict=ru`) | `volumeInfo.authors` |
| Amazon content endpoint | Manage Your Content and Devices, `.../hz/mycd/digital-console/ajax` (verify in DevTools) | Exact by ASIN, covers personal docs |

Google Books usually covers Russian/Ukrainian editions better. Clean titles first (strip `(… Book N)` and text after a colon), and accept a hit only when its title closely matches.

## Merge plan and SQLite tips

Merge in a script: load both JSON exports, normalize titles, collapse duplicates into one row with all sources joined (e.g. `Kindle (purchased) + Kindle (personal doc)`). Some books exist as both a purchase and an upload (e.g. *Two for the Lions*).

- Title normalization: lowercase, strip anything in parentheses, cut at the first colon; then match on title + author.
- Both queries emit the same columns (title, author, language, year, publisher, source); `NULL AS publisher` fills the gap on the Apple side.

| Task | Command |
| --- | --- |
| List tables | `.tables` |
| Columns of a table | `PRAGMA table_info(<table>);` or `.schema <table>` |
| Find a column by name | `SELECT name FROM pragma_table_info('<table>') WHERE name LIKE '%LANG%';` |
| Check file is plain SQLite | `head -c 16 <file>` prints `SQLite format 3` |
| Output modes | `.help mode`: `csv`, `tabs`, `json`, `html`, `insert`, `markdown`, `box`, `table`, `line`, `list` |
| CSV with headers (3.54) | `.mode csv --title on` |
| Write next result to a file | `.once <file>` |

Open items:

- [ ] Fill `author_map` for the largest author groups
- [ ] Check `ZRAWPUBLISHER` is readable (not encrypted)
- [ ] Identify book types 17 and 19
- [ ] Write the merge script
