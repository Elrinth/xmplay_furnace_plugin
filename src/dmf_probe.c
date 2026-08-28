#include "dmf_probe.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* X-Tracker DMF: little-endian chunked container, magic at offset 0. */
static const unsigned char k_xt_magic[4] = { 'D', 'D', 'M', 'F' };

/* DefleMask DMF: tracker dump, documented magic string. */
static const unsigned char k_deflemask_magic[16] = {
	'.', 'D', 'e', 'l', 'e', 'k', 'D', 'e', 'f', 'l', 'e', 'M', 'a', 's', 'k', '.'
};

/* Furnace native module (also commonly zlib-wrapped). */
static const unsigned char k_fur_magic[16] = {
	'-', 'F', 'u', 'r', 'n', 'a', 'c', 'e', ' ', 'm', 'o', 'd', 'u', 'l', 'e', '-'
};
static const unsigned char k_furb_magic[16] = {
	'F', 'u', 'r', 'n', 'a', 'c', 'e', '-', 'B', ' ', 'm', 'o', 'd', 'u', 'l', 'e'
};

/* FamiTracker / Dn-FamiTracker / TFM Music Maker / Future Composer. */
static const unsigned char k_ftm_magic[18] = {
	'F', 'a', 'm', 'i', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ',
	'M', 'o', 'd', 'u', 'l', 'e'
};
static const unsigned char k_dnm_magic[21] = {
	'D', 'n', '-', 'F', 'a', 'm', 'i', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ',
	'M', 'o', 'd', 'u', 'l', 'e'
};
static const unsigned char k_tfm_magic[8] = {
	'T', 'F', 'M', 'f', 'm', 't', 'V', '2'
};
static const unsigned char k_fc13_magic[4] = { 'S', 'M', 'O', 'D' };
static const unsigned char k_fc14_magic[4] = { 'F', 'C', '1', '4' };

int dmf_is_zlib_header(const unsigned char *buf, size_t len)
{
	unsigned flg;
	if (!buf || len < 2)
		return 0;
	if (buf[0] != 0x78)
		return 0;
	flg = buf[1];
	return (flg == 0x01 || flg == 0x9c || flg == 0xda);
}

int dmf_kind_claimed(int kind)
{
	return kind == DMF_KIND_DEFLEMASK
	    || kind == DMF_KIND_FURNACE
	    || kind == DMF_KIND_FURNACE_IMPORT;
}

int dmf_kind_furnace_exclusive(int kind)
{
	return dmf_kind_claimed(kind);
}

/* Inflate just enough to see the first `want` uncompressed bytes.
 * Returns number of bytes written to out, or 0 on failure.
 */
static size_t inflate_prefix(const unsigned char *buf, size_t len,
                             unsigned char *out, size_t want)
{
	z_stream zs;
	int err;
	size_t got;

	if (!buf || !out || want == 0 || len < 2)
		return 0;
	if (len > DMF_ZLIB_COMPRESSED_MAX)
		len = DMF_ZLIB_COMPRESSED_MAX;

	memset(&zs, 0, sizeof zs);
	zs.next_in = (Bytef *)buf;
	zs.avail_in = (uInt)len;
	if (inflateInit(&zs) != Z_OK)
		return 0;
	zs.next_out = out;
	zs.avail_out = (uInt)want;
	err = inflate(&zs, Z_SYNC_FLUSH);
	got = (size_t)zs.total_out;
	inflateEnd(&zs);
	if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
		return 0;
	return got;
}

static int probe_raw(const unsigned char *buf, size_t len)
{
	if (!buf || len < 2)
		return DMF_KIND_REJECT;

	if (len >= 16 && memcmp(buf, k_deflemask_magic, 16) == 0)
		return DMF_KIND_DEFLEMASK;
	if (len >= 16 && memcmp(buf, k_fur_magic, 16) == 0)
		return DMF_KIND_FURNACE;
	if (len >= 16 && memcmp(buf, k_furb_magic, 16) == 0)
		return DMF_KIND_FURNACE;
	if (len >= 21 && memcmp(buf, k_dnm_magic, 21) == 0)
		return DMF_KIND_FURNACE_IMPORT;
	if (len >= 18 && memcmp(buf, k_ftm_magic, 18) == 0)
		return DMF_KIND_FURNACE_IMPORT;
	if (len >= 8 && memcmp(buf, k_tfm_magic, 8) == 0)
		return DMF_KIND_OTHER;
	if (len >= 4 && memcmp(buf, k_xt_magic, 4) == 0)
		return DMF_KIND_XTRACKER;
	if (len >= 4 && (memcmp(buf, k_fc13_magic, 4) == 0 ||
	                 memcmp(buf, k_fc14_magic, 4) == 0))
		return DMF_KIND_OTHER;

	return DMF_KIND_REJECT;
}

int dmf_probe_kind(const unsigned char *buf, size_t len)
{
	unsigned char head[24];
	size_t n;
	int kind;

	if (!buf || len < 2)
		return DMF_KIND_REJECT;

	kind = probe_raw(buf, len);
	if (kind != DMF_KIND_REJECT)
		return kind;

	if (!dmf_is_zlib_header(buf, len))
		return DMF_KIND_REJECT;

	n = inflate_prefix(buf, len, head, sizeof head);
	if (n >= 4)
		return probe_raw(head, n);

	return DMF_KIND_REJECT;
}

int dmf_probe_magic(const unsigned char *buf, size_t len)
{
	/* Historical name: X-Tracker only. */
	return dmf_probe_kind(buf, len) == DMF_KIND_XTRACKER
		? DMF_KIND_XTRACKER : DMF_KIND_REJECT;
}

static void copy_sanitized(char *dst, size_t cap, const unsigned char *src, size_t n)
{
	size_t i, o = 0;
	if (!dst || cap == 0)
		return;
	if (!src)
		n = 0;
	for (i = 0; i < n && o + 1 < cap; ++i) {
		unsigned char c = src[i];
		if (c == 0)
			break;
		if (c < 32 || c == 127)
			c = ' ';
		dst[o++] = (char)c;
	}
	dst[o] = '\0';
}

/* Inflate whole stream into *out (malloc), capped. Returns 1 on success. */
static int inflate_all(const unsigned char *buf, size_t len,
                       unsigned char **out, size_t *out_len)
{
	z_stream zs;
	unsigned char *acc = NULL;
	size_t acc_len = 0, acc_cap = 0;
	int err;

	*out = NULL;
	*out_len = 0;
	if (!buf || len < 2)
		return 0;
	if (len > DMF_ZLIB_COMPRESSED_MAX)
		return 0;

	memset(&zs, 0, sizeof zs);
	zs.next_in = (Bytef *)buf;
	zs.avail_in = (uInt)len;
	if (inflateInit(&zs) != Z_OK)
		return 0;

	for (;;) {
		unsigned char tmp[16384];
		zs.next_out = tmp;
		zs.avail_out = sizeof tmp;
		err = inflate(&zs, Z_NO_FLUSH);
		{
			size_t chunk = sizeof tmp - zs.avail_out;
			if (chunk) {
				if (acc_len + chunk > DMF_ZLIB_UNCOMPRESSED_MAX) {
					inflateEnd(&zs);
					free(acc);
					return 0;
				}
				if (acc_len + chunk > acc_cap) {
					size_t ncap = acc_cap ? acc_cap * 2 : 65536;
					unsigned char *nb;
					while (ncap < acc_len + chunk)
						ncap *= 2;
					if (ncap > DMF_ZLIB_UNCOMPRESSED_MAX)
						ncap = DMF_ZLIB_UNCOMPRESSED_MAX;
					nb = (unsigned char *)realloc(acc, ncap);
					if (!nb) {
						inflateEnd(&zs);
						free(acc);
						return 0;
					}
					acc = nb;
					acc_cap = ncap;
				}
				memcpy(acc + acc_len, tmp, chunk);
				acc_len += chunk;
			}
		}
		if (err == Z_STREAM_END)
			break;
		if (err != Z_OK) {
			inflateEnd(&zs);
			free(acc);
			return 0;
		}
		if (zs.avail_in == 0 && zs.avail_out == sizeof tmp)
			break;
	}
	inflateEnd(&zs);
	if (acc_len < 16) {
		free(acc);
		return 0;
	}
	*out = acc;
	*out_len = acc_len;
	return 1;
}

int dmf_deflemask_meta(const unsigned char *buf, size_t len,
                       char *title, size_t title_cap,
                       char *author, size_t author_cap,
                       int *version, int *system)
{
	const unsigned char *body = buf;
	size_t body_len = len;
	unsigned char *owned = NULL;
	size_t owned_len = 0;
	size_t pos;
	unsigned namelen, authlen;
	int ok = 0;

	if (title && title_cap)
		title[0] = '\0';
	if (author && author_cap)
		author[0] = '\0';
	if (version)
		*version = 0;
	if (system)
		*system = 0;
	if (!buf || len < 16)
		return 0;

	if (dmf_is_zlib_header(buf, len)) {
		if (!inflate_all(buf, len, &owned, &owned_len))
			return 0;
		body = owned;
		body_len = owned_len;
	}

	if (body_len < 18 || memcmp(body, k_deflemask_magic, 16) != 0)
		goto done;

	/* version @ 16, system @ 17 (v>=0x09), then u8-length strings. */
	if (version)
		*version = (int)body[16];
	pos = 17;
	if (body[16] >= 0x09) {
		if (body_len < 19)
			goto done;
		if (system)
			*system = (int)body[17];
		pos = 18;
	}
	if (pos >= body_len)
		goto done;
	namelen = body[pos++];
	if (pos + namelen > body_len)
		goto done;
	copy_sanitized(title, title_cap, body + pos, namelen);
	pos += namelen;
	if (pos >= body_len)
		goto done;
	authlen = body[pos++];
	if (pos + authlen > body_len)
		goto done;
	copy_sanitized(author, author_cap, body + pos, authlen);
	ok = 1;

done:
	free(owned);
	return ok;
}


/* CheckFile-equivalent: Furnace / DefleMask / FamiTracker. Never DDMF. */
int xmp_dmf_check(const unsigned char *buf, size_t len)
{
	int kind;

	if (!buf || len < 2)
		return 0;
	kind = dmf_probe_kind(buf, len);
	if (dmf_kind_claimed(kind))
		return 1;
	/* zlib prefix may need a larger slurp to see .fur / DefleMask. */
	if (dmf_is_zlib_header(buf, len) && kind == DMF_KIND_REJECT)
		return -1;
	return 0;
}
