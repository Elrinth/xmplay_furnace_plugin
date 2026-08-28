/*
 * xmp-furnace — crash-safe native XMPlay input plugin for
 *   Furnace (.fur), DefleMask (.dmf), and FamiTracker (.ftm/.0cc/.dnm/.eft).
 *
 * Engine: headless Furnace (tildearrow) DivEngine only. No libopenmpt.
 * X-Tracker DDMF is rejected — use xmp-openmpt.
 *
 * DllMain / XMPIN_GetInterface never construct a decoder.
 * Does not wrap any Winamp in_* plugin.
 * Does not register xm/it/s3m/mod/mtm/mo3/umx or .tfe/.fc.
 */
#if defined(__GNUC__)
#define XMPIN_GetInterface XMPIN_GetInterface_Declared
#endif
#include "xmpin.h"
#if defined(__GNUC__)
#undef XMPIN_GetInterface
#endif

#include "dmf_probe.h"
#include "furnace_player.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define PLUGIN_NAME "Furnace / DefleMask"
#define PLUGIN_VERSION "1.0.0"
#define MAX_MODULE_BYTES ((size_t)128u * 1024u * 1024u)
#define PREFIX_BYTES 2048
#define ZLIB_PROBE_BYTES ((size_t)64u * 1024u)
#define INFO_WRITE_MAX 32766
#define DEFAULT_RATE 48000
#define MIN_RATE 8000
#define MAX_RATE 192000

static XMPFUNC_IN *xmpfin;
static XMPFUNC_MISC *xmpfmisc;
static XMPFUNC_FILE *xmpffile;

static furnace_player *g_furn;
static int32_t g_rate = DEFAULT_RATE;
static int32_t g_channels = 2;
static int32_t g_nsubsongs = 1;
static float g_subsong_total;
static char g_name_hint[512];
static char g_filetype[16];
static int g_src_kind;

static void remember_hint(const char *filename)
{
	size_t n;
	g_name_hint[0] = '\0';
	if (!filename || !filename[0])
		return;
	n = strlen(filename);
	if (n >= sizeof g_name_hint)
		n = sizeof g_name_hint - 1;
	memcpy(g_name_hint, filename, n);
	g_name_hint[n] = '\0';
}

static const char *ext_of(const char *filename)
{
	const char *dot, *p;
	if (!filename || !filename[0])
		return "";
	dot = NULL;
	for (p = filename; *p; ++p) {
		if (*p == '.' || *p == '/' || *p == '\\')
			dot = (*p == '.') ? p : NULL;
	}
	return dot ? dot : "";
}

static int ext_ieq(const char *ext, const char *want)
{
	size_t i;
	if (!ext || !want)
		return 0;
	for (i = 0; ext[i] || want[i]; ++i) {
		unsigned char a = (unsigned char)ext[i];
		unsigned char b = (unsigned char)want[i];
		if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

static const char *furnace_filetype(int kind, const char *filename)
{
	const char *ext = ext_of(filename);
	if (ext_ieq(ext, ".dmf"))
		return "DMF";
	if (ext_ieq(ext, ".fur"))
		return "FUR";
	if (ext_ieq(ext, ".ftm"))
		return "FTM";
	if (ext_ieq(ext, ".0cc"))
		return "0CC";
	if (ext_ieq(ext, ".dnm"))
		return "DNM";
	if (ext_ieq(ext, ".eft"))
		return "EFT";
	if (kind == DMF_KIND_DEFLEMASK)
		return "DMF";
	if (kind == DMF_KIND_FURNACE)
		return "FUR";
	if (kind == DMF_KIND_FURNACE_IMPORT)
		return "FTM";
	return "FUR";
}

static void bounded_copy(char *dst, size_t cap, const char *src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void sanitize_line(char *s)
{
	if (!s)
		return;
	for (; *s; ++s) {
		if (*s == '\t' || *s == '\r' || *s == '\n')
			*s = ' ';
	}
}

static void write_kv(char **cursor, char *end, const char *name, const char *value)
{
	size_t nl, vl, need;
	if (!cursor || !*cursor || !end || !name || !value || !value[0])
		return;
	nl = strlen(name);
	vl = strlen(value);
	need = nl + 1 + vl + 1;
	if (*cursor + need >= end)
		return;
	memcpy(*cursor, name, nl);
	*cursor += nl;
	**cursor = '\t';
	*cursor += 1;
	memcpy(*cursor, value, vl);
	*cursor += vl;
	**cursor = '\r';
	*cursor += 1;
	**cursor = '\0';
}

static void *xmp_alloc(DWORD n)
{
	if (!xmpfmisc || !xmpfmisc->Alloc || n == 0)
		return NULL;
	return xmpfmisc->Alloc(n);
}

static int read_prefix(XMPFILE file, unsigned char *buf, size_t cap, size_t *got)
{
	DWORD n;
	DWORD pos;
	DWORD type;

	if (got)
		*got = 0;
	if (!file || !buf || cap == 0 || !xmpffile)
		return 0;
	if (!xmpffile->GetType || !xmpffile->Read)
		return 0;

	type = xmpffile->GetType(file);
	if (type == XMPFILE_TYPE_MEMORY) {
		const void *mem;
		DWORD sz;
		if (!xmpffile->GetMemory || !xmpffile->GetSize)
			return 0;
		mem = xmpffile->GetMemory(file);
		sz = xmpffile->GetSize(file);
		if (!mem || sz == 0)
			return 0;
		if ((size_t)sz > cap)
			sz = (DWORD)cap;
		memcpy(buf, mem, sz);
		if (got)
			*got = sz;
		return 1;
	}

	pos = 0;
	if (xmpffile->Tell)
		pos = xmpffile->Tell(file);
	if (xmpffile->Seek)
		xmpffile->Seek(file, 0);
	n = xmpffile->Read(file, buf, (DWORD)cap);
	if (xmpffile->Seek)
		xmpffile->Seek(file, pos);
	if (got)
		*got = n;
	return n > 0;
}

static int slurp_xmpfile(XMPFILE file, unsigned char **out, size_t *out_len)
{
	DWORD type;
	DWORD sz;
	unsigned char *buf;
	DWORD got;
	DWORD pos;

	if (out)
		*out = NULL;
	if (out_len)
		*out_len = 0;
	if (!file || !out || !out_len || !xmpffile)
		return 0;
	if (!xmpffile->GetType || !xmpffile->Read)
		return 0;

	type = xmpffile->GetType(file);

	if (type == XMPFILE_TYPE_MEMORY) {
		const void *mem;
		if (!xmpffile->GetMemory || !xmpffile->GetSize)
			return 0;
		mem = xmpffile->GetMemory(file);
		sz = xmpffile->GetSize(file);
		if (!mem || sz < 2 || (size_t)sz > MAX_MODULE_BYTES)
			return 0;
		buf = (unsigned char *)malloc(sz);
		if (!buf)
			return 0;
		memcpy(buf, mem, sz);
		*out = buf;
		*out_len = sz;
		return 1;
	}

	sz = xmpffile->GetSize ? xmpffile->GetSize(file) : 0;
	pos = xmpffile->Tell ? xmpffile->Tell(file) : 0;
	if (xmpffile->Seek)
		xmpffile->Seek(file, 0);

	if (sz > 0) {
		if (sz < 2 || (size_t)sz > MAX_MODULE_BYTES) {
			if (xmpffile->Seek)
				xmpffile->Seek(file, pos);
			return 0;
		}
		buf = (unsigned char *)malloc(sz);
		if (!buf) {
			if (xmpffile->Seek)
				xmpffile->Seek(file, pos);
			return 0;
		}
		got = xmpffile->Read(file, buf, sz);
		if (xmpffile->Seek)
			xmpffile->Seek(file, pos);
		if (got < 2) {
			free(buf);
			return 0;
		}
		*out = buf;
		*out_len = got;
		return 1;
	}

	{
		size_t cap = 64 * 1024;
		size_t total = 0;
		buf = (unsigned char *)malloc(cap);
		if (!buf)
			return 0;
		for (;;) {
			DWORD chunk;
			if (total == cap) {
				size_t ncap = cap * 2;
				unsigned char *nb;
				if (ncap > MAX_MODULE_BYTES)
					ncap = MAX_MODULE_BYTES;
				if (ncap <= cap) {
					free(buf);
					return 0;
				}
				nb = (unsigned char *)realloc(buf, ncap);
				if (!nb) {
					free(buf);
					return 0;
				}
				buf = nb;
				cap = ncap;
			}
			chunk = xmpffile->Read(file, buf + total, (DWORD)(cap - total));
			if (chunk == 0)
				break;
			total += chunk;
			if (total >= MAX_MODULE_BYTES)
				break;
		}
		if (xmpffile->Seek)
			xmpffile->Seek(file, pos);
		if (total < 2) {
			free(buf);
			return 0;
		}
		*out = buf;
		*out_len = total;
		return 1;
	}
}

static XMPFILE open_if_needed(const char *filename, XMPFILE file, int *opened)
{
	*opened = 0;
	if (file)
		return file;
	if (!filename || !xmpffile || !xmpffile->Open)
		return NULL;
	file = xmpffile->Open(filename);
	if (file)
		*opened = 1;
	return file;
}

static void close_if_opened(XMPFILE file, int opened)
{
	if (opened && file && xmpffile && xmpffile->Close)
		xmpffile->Close(file);
}

static int probe_xmpfile(XMPFILE file, const char *filename)
{
	unsigned char prefix[PREFIX_BYTES];
	size_t got = 0;
	int kind;
	int decided;
	DWORD sz = 0;

	(void)filename;
	memset(prefix, 0, sizeof prefix);
	if (!read_prefix(file, prefix, sizeof prefix, &got))
		return 0;
	if (xmpffile && xmpffile->GetSize)
		sz = xmpffile->GetSize(file);

	decided = xmp_dmf_check(prefix, got);
	if (decided == 1)
		return 1;
	if (decided == 0 && !dmf_is_zlib_header(prefix, got))
		return 0;

	if (dmf_is_zlib_header(prefix, got)) {
		unsigned char *buf = NULL;
		size_t len = 0;
		DWORD type = xmpffile && xmpffile->GetType ? xmpffile->GetType(file) : 0;
		if ((size_t)sz > DMF_ZLIB_COMPRESSED_MAX && sz > 0)
			return 0;
		if (type == XMPFILE_TYPE_MEMORY && xmpffile->GetMemory && sz > 0) {
			const void *mem = xmpffile->GetMemory(file);
			if (!mem)
				return 0;
			kind = dmf_probe_kind((const unsigned char *)mem, (size_t)sz);
			return dmf_kind_claimed(kind);
		}
		if (!slurp_xmpfile(file, &buf, &len))
			return 0;
		if (len > ZLIB_PROBE_BYTES)
			len = ZLIB_PROBE_BYTES;
		kind = dmf_probe_kind(buf, len);
		free(buf);
		return dmf_kind_claimed(kind);
	}

	return 0;
}

static void append_tag(char **p, char *end, const char *key, const char *val)
{
	size_t kl, vl;
	if (!p || !*p || !end || !key || !val || !val[0])
		return;
	kl = strlen(key);
	vl = strlen(val);
	if (*p + kl + 1 + vl + 1 + 1 >= end)
		return;
	memcpy(*p, key, kl);
	*p += kl;
	**p = '\0';
	*p += 1;
	memcpy(*p, val, vl);
	*p += vl;
	**p = '\0';
	*p += 1;
}

static char *finish_tags(char *stack, char *p, size_t stack_sz)
{
	char *end = stack + stack_sz;
	char *out;
	size_t n;
	if (p + 1 < end)
		*p++ = '\0';
	n = (size_t)(p - stack);
	out = (char *)xmp_alloc((DWORD)n);
	if (!out)
		return NULL;
	memcpy(out, stack, n);
	return out;
}

static char *build_tags_furnace(const char *filetype, const char *title,
                                const char *author, const char *comment)
{
	char stack[8192];
	char *p = stack;
	char *end = stack + sizeof stack;

	if (!xmpfmisc)
		return NULL;
	append_tag(&p, end, "filetype", filetype && filetype[0] ? filetype : "FUR");
	append_tag(&p, end, "title", title);
	append_tag(&p, end, "artist", author);
	append_tag(&p, end, "comment", comment);
	append_tag(&p, end, "encoder", "Furnace");
	return finish_tags(stack, p, sizeof stack);
}

static void unload_playback(void)
{
	if (g_furn) {
		furnace_player_close(g_furn);
		g_furn = NULL;
	}
	g_nsubsongs = 1;
	g_subsong_total = 0.0f;
	g_rate = DEFAULT_RATE;
	g_channels = 2;
	g_name_hint[0] = '\0';
	g_filetype[0] = '\0';
	g_src_kind = DMF_KIND_REJECT;
}

static int adopt_furnace(unsigned char *data, size_t len, const char *filename)
{
	if (len > DMF_ZLIB_COMPRESSED_MAX && data && data[0] == 0x78) {
		free(data);
		return 0;
	}
	g_furn = furnace_player_open(data, len, DEFAULT_RATE, filename);
	free(data);
	if (!g_furn)
		return 0;
	g_rate = furnace_player_rate(g_furn);
	g_channels = 2;
	g_nsubsongs = 1;
	g_subsong_total = (float)furnace_player_duration(g_furn);
	if (xmpfin && xmpfin->SetLength && g_subsong_total > 0.0f && g_subsong_total < 86400.0f)
		xmpfin->SetLength(g_subsong_total, TRUE);
	return 1;
}

static void WINAPI fur_About(HWND win)
{
	char buf[1024];
	snprintf(buf, sizeof buf,
		PLUGIN_NAME " " PLUGIN_VERSION "\r\n"
		"Native XMPlay input plugin for Furnace, DefleMask, and\r\n"
		"FamiTracker family modules.\r\n\r\n"
		"  .fur — Furnace native\r\n"
		"  .dmf — DefleMask (raw or zlib)\r\n"
		"  .ftm / .0cc / .dnm / .eft — FamiTracker imports\r\n\r\n"
		"Does NOT play X-Tracker DDMF — use OpenMPT (xmp-openmpt).\r\n"
		"Does not wrap any Winamp in_* plugin.\r\n"
		"Does not register xm / it / s3m / mod.\r\n\r\n"
		"Engine: Furnace (tildearrow) headless DivEngine.\r\n"
		"License: GPLv2 (Furnace is GPLv2-or-later).");
	MessageBoxA(win, buf, PLUGIN_NAME, MB_OK | MB_ICONINFORMATION);
}

static BOOL WINAPI fur_CheckFile(const char *filename, XMPFILE file)
{
	int opened = 0;
	int ok;

	file = open_if_needed(filename, file, &opened);
	if (!file) {
		(void)filename;
		return FALSE;
	}
	ok = probe_xmpfile(file, filename);
	close_if_opened(file, opened);
	return ok ? TRUE : FALSE;
}

static DWORD WINAPI fur_GetFileInfo(const char *filename, XMPFILE file, float **length, char **tags)
{
	unsigned char *data = NULL;
	size_t len = 0;
	int opened = 0;
	int kind;
	const char *ft;

	if (length)
		*length = NULL;
	if (tags)
		*tags = NULL;

	file = open_if_needed(filename, file, &opened);
	if (!file)
		return 0;
	if (!slurp_xmpfile(file, &data, &len)) {
		close_if_opened(file, opened);
		return 0;
	}
	close_if_opened(file, opened);

	kind = dmf_probe_kind(data, len);
	if (!dmf_kind_claimed(kind)) {
		free(data);
		return 0;
	}
	ft = furnace_filetype(kind, filename);
	if (length) {
		float *lens = (float *)xmp_alloc((DWORD)sizeof(float));
		if (lens)
			lens[0] = 0.0f;
		*length = lens;
	}
	if (tags) {
		char title[256], author[256];
		title[0] = author[0] = '\0';
		if (kind == DMF_KIND_DEFLEMASK)
			dmf_deflemask_meta(data, len, title, sizeof title,
			                   author, sizeof author, NULL, NULL);
		*tags = build_tags_furnace(ft, title, author, NULL);
	}
	free(data);
	return 1;
}

static DWORD WINAPI fur_Open(const char *filename, XMPFILE file)
{
	unsigned char *data = NULL;
	size_t len = 0;
	int opened = 0;
	int kind;
	const char *hint;

	unload_playback();
	remember_hint(filename);

	file = open_if_needed(filename, file, &opened);
	if (!file)
		return 0;
	if (!slurp_xmpfile(file, &data, &len)) {
		close_if_opened(file, opened);
		return 0;
	}
	close_if_opened(file, opened);

	kind = dmf_probe_kind(data, len);
	g_src_kind = kind;
	bounded_copy(g_filetype, sizeof g_filetype, furnace_filetype(kind, filename));
	if (!dmf_kind_claimed(kind)) {
		free(data);
		return 0;
	}
	hint = filename && filename[0] ? filename : g_name_hint;
	if (!adopt_furnace(data, len, hint))
		return 0;
	return 2;
}

static void WINAPI fur_Close(void)
{
	unload_playback();
}

static void WINAPI fur_SetFormat(XMPFORMAT *form)
{
	if (!form)
		return;
	if (!g_furn) {
		form->rate = 0;
		form->chan = 0;
		form->res = 0;
		form->chanmask = 0;
		return;
	}
	form->rate = (DWORD)furnace_player_rate(g_furn);
	form->chan = 2;
	form->res = 4;
	form->chanmask = 0;
	g_rate = (int32_t)form->rate;
	g_channels = 2;
}

static char *WINAPI fur_GetTags(void)
{
	if (!g_furn)
		return NULL;
	return build_tags_furnace(g_filetype[0] ? g_filetype :
	                          furnace_filetype(g_src_kind, g_name_hint),
	                          furnace_player_title(g_furn),
	                          furnace_player_author(g_furn),
	                          furnace_player_notes(g_furn));
}

static void WINAPI fur_GetInfoText(char *format, char *length)
{
	char tmp[256];
	int m, s;

	if (format)
		format[0] = '\0';
	if (length)
		length[0] = '\0';
	if (!g_furn)
		return;

	if (format) {
		snprintf(tmp, sizeof tmp, "%s  %d ch  (%s)",
		         g_filetype[0] ? g_filetype : "FUR",
		         furnace_player_channels(g_furn),
		         furnace_player_system(g_furn));
		sanitize_line(tmp);
		bounded_copy(format, 256, tmp);
	}
	if (length) {
		double dur = furnace_player_duration(g_furn);
		int orders = furnace_player_orders(g_furn);
		m = (int)(dur / 60.0);
		s = (int)(dur) % 60;
		snprintf(tmp, sizeof tmp, "%d:%02d  %d orders", m, s, orders);
		sanitize_line(tmp);
		bounded_copy(length, 256, tmp);
	}
}

static void WINAPI fur_GetGeneralInfo(char *buf)
{
	char local[4096];
	char *p;
	char *end;
	char num[32];

	if (!buf)
		return;
	buf[0] = '\0';
	if (!g_furn)
		return;

	p = local;
	end = local + sizeof local - 2;
	local[0] = '\0';

	write_kv(&p, end, "Title", furnace_player_title(g_furn));
	write_kv(&p, end, "Artist", furnace_player_author(g_furn));
	write_kv(&p, end, "Format", furnace_player_system(g_furn));
	write_kv(&p, end, "Type", g_filetype[0] ? g_filetype : "FUR");
	write_kv(&p, end, "System", furnace_player_system(g_furn));
	snprintf(num, sizeof num, "%d", furnace_player_channels(g_furn));
	write_kv(&p, end, "Channels", num);
	snprintf(num, sizeof num, "%d", furnace_player_orders(g_furn));
	write_kv(&p, end, "Orders", num);
	snprintf(num, sizeof num, "%d", furnace_player_patlen(g_furn));
	write_kv(&p, end, "Pattern length", num);
	snprintf(num, sizeof num, "%d", furnace_player_version(g_furn));
	write_kv(&p, end, "Module version", num);
	write_kv(&p, end, "Player", PLUGIN_NAME " " PLUGIN_VERSION);
	write_kv(&p, end, "Engine", "Furnace (headless)");
	bounded_copy(buf, INFO_WRITE_MAX, local);
}

static void WINAPI fur_GetMessage(char *buf)
{
	const char *msg;
	char *tmp;
	size_t i, n;

	if (!buf)
		return;
	buf[0] = '\0';
	if (!g_furn)
		return;
	msg = furnace_player_notes(g_furn);
	if (!msg || !msg[0])
		return;
	n = strlen(msg);
	if (n > INFO_WRITE_MAX - 1)
		n = INFO_WRITE_MAX - 1;
	tmp = (char *)malloc(n + 1);
	if (!tmp)
		return;
	memcpy(tmp, msg, n);
	tmp[n] = '\0';
	for (i = 0; i < n; ++i) {
		if (tmp[i] == '\n')
			tmp[i] = '\r';
		else if (tmp[i] == '\t')
			tmp[i] = ' ';
	}
	bounded_copy(buf, INFO_WRITE_MAX, tmp);
	free(tmp);
}

static double WINAPI fur_GetGranularity(void)
{
	return 0.001;
}

static double WINAPI fur_SetPosition(DWORD pos)
{
	double t;

	if (!g_furn)
		return -1.0;
	if (pos == (DWORD)XMPIN_POS_LOOP || pos == (DWORD)XMPIN_POS_AUTOLOOP)
		return -2.0;
	if (pos & XMPIN_POS_SUBSONG)
		return 0.0;
	t = furnace_player_seek_ms(g_furn, (double)pos * fur_GetGranularity() * 1000.0);
	if (t < 0.0) {
		if (pos == 0)
			return furnace_player_seek_ms(g_furn, 0.0);
		return -1.0;
	}
	return t;
}

static DWORD WINAPI fur_Process(float *buf, DWORD count)
{
	size_t frames;
	int n;

	if (!buf || !g_furn || g_channels <= 0 || g_rate < MIN_RATE)
		return 0;
	frames = (size_t)count / 2u;
	if (frames == 0)
		return 0;
	if (frames > 65536)
		frames = 65536;
	n = furnace_player_process(g_furn, buf, (int)frames);
	if (n <= 0)
		return 0;
	return (DWORD)((size_t)n * 2u);
}

static DWORD WINAPI fur_GetSubSongs(float *length)
{
	if (length)
		*length = g_subsong_total;
	if (g_furn)
		return 1;
	return 0;
}

static void WINAPI fur_GetSamples(char *buf)
{
	char local[8192];
	char *p;
	char *end;
	int32_t n, i;

	if (!buf)
		return;
	buf[0] = '\0';
	if (!g_furn)
		return;

	p = local;
	end = local + sizeof local - 2;
	local[0] = '\0';
	n = furnace_player_ins_count(g_furn);
	if (n > 256)
		n = 256;
	for (i = 0; i < n; ++i) {
		const char *name = furnace_player_ins_name(g_furn, (int)i);
		char key[16];
		snprintf(key, sizeof key, "%02d", (int)(i + 1));
		write_kv(&p, end, key, (name && name[0]) ? name : "-");
	}
	bounded_copy(buf, INFO_WRITE_MAX, local);
}

/* Description\0ext1/ext2/...  — single string, no XMPIN_FLAG_MULTIEXT. */
static const char g_exts[] =
	"Furnace / DefleMask\0"
	"fur/dmf/ftm/0cc/dnm/eft";

static XMPIN g_xmpin = {
	0,
	PLUGIN_NAME " " PLUGIN_VERSION,
	g_exts,
	fur_About,
	NULL,
	fur_CheckFile,
	fur_GetFileInfo,
	fur_Open,
	fur_Close,
	NULL,
	fur_SetFormat,
	fur_GetTags,
	fur_GetInfoText,
	fur_GetGeneralInfo,
	fur_GetMessage,
	fur_SetPosition,
	fur_GetGranularity,
	NULL,
	fur_Process,
	NULL,
	fur_GetSamples,
	fur_GetSubSongs,
	NULL,
	NULL,
	NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL,
	NULL,
	NULL
};

static XMPIN *WINAPI xmpin_get_interface_impl(DWORD face, InterfaceProc faceproc)
{
	if (face != XMPIN_FACE)
		return NULL;
	if (!faceproc)
		return NULL;
	xmpfin = (XMPFUNC_IN *)faceproc(XMPFUNC_IN_FACE);
	xmpfmisc = (XMPFUNC_MISC *)faceproc(XMPFUNC_MISC_FACE);
	xmpffile = (XMPFUNC_FILE *)faceproc(XMPFUNC_FILE_FACE);
	if (!xmpfin || !xmpfmisc || !xmpffile)
		return NULL;
	if (!xmpfmisc->Alloc || !xmpffile->Read)
		return NULL;
	return &g_xmpin;
}

extern "C" {

BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hDLL);
	return TRUE;
}

#if defined(__GNUC__) && defined(_WIN32) && !defined(_WIN64)
XMPIN *WINAPI XMPIN_GetInterface_(DWORD face, InterfaceProc faceproc)
{
	return xmpin_get_interface_impl(face, faceproc);
}
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattribute-alias"
#endif
__attribute__((dllexport)) void XMPIN_GetInterface(void)
	__attribute__((alias("XMPIN_GetInterface_@8")));
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
#else
__declspec(dllexport) XMPIN *WINAPI XMPIN_GetInterface(DWORD face, InterfaceProc faceproc)
{
	return xmpin_get_interface_impl(face, faceproc);
}
#endif

} /* extern "C" */
