#include "dmf_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect(int cond, const char *name)
{
	if (cond) {
		printf("  PASS  %s\n", name);
	} else {
		printf("  FAIL  %s\n", name);
		g_failed = 1;
	}
}

static unsigned char *read_file(const char *path, size_t *out_len)
{
	FILE *f;
	unsigned char *buf;
	long sz;
	*out_len = 0;
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	sz = ftell(f);
	if (sz < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	buf = (unsigned char *)malloc((size_t)sz);
	if (!buf) { fclose(f); return NULL; }
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf); fclose(f); return NULL;
	}
	fclose(f);
	*out_len = (size_t)sz;
	return buf;
}

int main(void)
{
	unsigned char ddmf[8] = { 'D', 'D', 'M', 'F', 4, 'X', 'T', 'R' };
	unsigned char defle[16] = {
		'.', 'D', 'e', 'l', 'e', 'k', 'D', 'e', 'f', 'l', 'e', 'M', 'a', 's', 'k', '.'
	};
	unsigned char fur[16] = {
		'-', 'F', 'u', 'r', 'n', 'a', 'c', 'e', ' ', 'm', 'o', 'd', 'u', 'l', 'e', '-'
	};
	unsigned char furb[16] = {
		'F', 'u', 'r', 'n', 'a', 'c', 'e', '-', 'B', ' ', 'm', 'o', 'd', 'u', 'l', 'e'
	};
	unsigned char ftm[18] = {
		'F', 'a', 'm', 'i', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ',
		'M', 'o', 'd', 'u', 'l', 'e'
	};
	unsigned char dnm[21] = {
		'D', 'n', '-', 'F', 'a', 'm', 'i', 'T', 'r', 'a', 'c', 'k', 'e', 'r', ' ',
		'M', 'o', 'd', 'u', 'l', 'e'
	};
	unsigned char tfm[8] = { 'T', 'F', 'M', 'f', 'm', 't', 'V', '2' };
	unsigned char fc14[4] = { 'F', 'C', '1', '4' };
	unsigned char junk[8] = { 0x00, 0x01, 0x02, 0x03, 0x89, 0x50, 0x4E, 0x47 };
	unsigned char short_dd[3] = { 'D', 'D', 'M' };
	unsigned char fake_zlib[8] = { 0x78, 0x9c, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
	unsigned char impm[256];
	unsigned char *sample = NULL;
	size_t sample_len = 0;
	char title[128], author[128];
	int ver = 0, sys = 0;

	printf("xmp-furnace magic-byte probe tests\n");

	expect(dmf_probe_kind(defle, sizeof defle) == DMF_KIND_DEFLEMASK, "kind: raw DefleMask magic");
	expect(dmf_probe_kind(fur, sizeof fur) == DMF_KIND_FURNACE, "kind: -Furnace module-");
	expect(dmf_probe_kind(furb, sizeof furb) == DMF_KIND_FURNACE, "kind: Furnace-B module");
	expect(dmf_probe_kind(ftm, sizeof ftm) == DMF_KIND_FURNACE_IMPORT, "kind: FamiTracker Module");
	expect(dmf_probe_kind(dnm, sizeof dnm) == DMF_KIND_FURNACE_IMPORT, "kind: Dn-FamiTracker Module");
	expect(xmp_dmf_check(defle, sizeof defle) == 1, "CheckFile: accept DefleMask");
	expect(xmp_dmf_check(fur, sizeof fur) == 1, "CheckFile: accept Furnace .fur");
	expect(xmp_dmf_check(furb, sizeof furb) == 1, "CheckFile: accept Furnace-B");
	expect(xmp_dmf_check(ftm, sizeof ftm) == 1, "CheckFile: accept FamiTracker");
	expect(xmp_dmf_check(dnm, sizeof dnm) == 1, "CheckFile: accept Dn-FamiTracker");

	/* X-Tracker DDMF must never be claimed. */
	expect(dmf_probe_kind(ddmf, sizeof ddmf) == DMF_KIND_XTRACKER, "kind: DDMF identified");
	expect(xmp_dmf_check(ddmf, sizeof ddmf) == 0, "CheckFile: REJECT DDMF / X-Tracker");
	expect(xmp_dmf_check(ddmf, 4) == 0, "CheckFile: REJECT exact 4-byte DDMF");

	/* TFM / FC identified but not claimed (ZXTUNE has TFE). */
	expect(dmf_probe_kind(tfm, sizeof tfm) == DMF_KIND_OTHER, "kind: TFM is other");
	expect(dmf_probe_kind(fc14, sizeof fc14) == DMF_KIND_OTHER, "kind: FC14 is other");
	expect(xmp_dmf_check(tfm, sizeof tfm) == 0, "CheckFile: reject TFM / .tfe");
	expect(xmp_dmf_check(fc14, sizeof fc14) == 0, "CheckFile: reject FC14");

	memset(impm, 0, sizeof impm);
	memcpy(impm, "IMPM", 4);
	expect(dmf_probe_kind(impm, sizeof impm) == DMF_KIND_REJECT, "kind: IMPM is not ours");
	expect(xmp_dmf_check(impm, sizeof impm) == 0, "CheckFile: reject IMPM");
	expect(dmf_probe_kind(junk, sizeof junk) == DMF_KIND_REJECT, "kind: reject garbage / PNG");
	expect(xmp_dmf_check(junk, sizeof junk) == 0, "CheckFile: reject garbage");
	expect(dmf_probe_kind(NULL, 8) == DMF_KIND_REJECT, "kind: reject NULL buffer");
	expect(dmf_probe_kind(ddmf, 0) == DMF_KIND_REJECT, "kind: reject zero length");
	expect(dmf_probe_kind(short_dd, 3) == DMF_KIND_REJECT, "kind: reject 3-byte prefix");
	expect(dmf_probe_kind(fake_zlib, sizeof fake_zlib) == DMF_KIND_REJECT,
	       "kind: reject zlib header that is not a known magic");
	expect(xmp_dmf_check(fake_zlib, sizeof fake_zlib) != 1,
	       "CheckFile: fake zlib is not claimed");

	expect(dmf_kind_claimed(DMF_KIND_DEFLEMASK), "claimed: DefleMask");
	expect(dmf_kind_claimed(DMF_KIND_FURNACE), "claimed: .fur");
	expect(dmf_kind_claimed(DMF_KIND_FURNACE_IMPORT), "claimed: FamiTracker");
	expect(!dmf_kind_claimed(DMF_KIND_XTRACKER), "claimed: DDMF is NOT claimed");
	expect(!dmf_kind_claimed(DMF_KIND_OTHER), "claimed: TFM/FC is NOT claimed");

	sample = read_file("tests/samples/doom-hanger.dmf", &sample_len);
	if (!sample)
		sample = read_file("/workspace/xmp-furnace/tests/samples/doom-hanger.dmf", &sample_len);
	if (sample) {
		expect(sample_len > 2 && sample[0] == 0x78, "sample dmf: zlib CMF 0x78");
		expect(dmf_probe_kind(sample, sample_len) == DMF_KIND_DEFLEMASK,
		       "sample dmf: zlib DefleMask accepted");
		expect(xmp_dmf_check(sample, sample_len) == 1,
		       "sample dmf CheckFile: accept");
		expect(dmf_deflemask_meta(sample, sample_len, title, sizeof title,
		                          author, sizeof author, &ver, &sys),
		       "sample dmf: meta parse");
		expect(ver == 24, "sample dmf: version 24");
		expect(sys == 0x02, "sample dmf: system Genesis 0x02");
		printf("  info  title='%s' author='%s'\n", title, author);
		expect(strstr(title, "Doom") != NULL, "sample dmf: title contains Doom");
		free(sample);
	} else {
		printf("  SKIP  doom-hanger.dmf not found\n");
	}

	sample = read_file("tests/samples/quickstart.fur", &sample_len);
	if (!sample)
		sample = read_file("/workspace/xmp-furnace/tests/samples/quickstart.fur", &sample_len);
	if (sample) {
		expect(dmf_probe_kind(sample, sample_len) == DMF_KIND_FURNACE,
		       "sample fur: Furnace magic accepted");
		expect(xmp_dmf_check(sample, sample_len) == 1,
		       "sample fur CheckFile: accept");
		free(sample);
	} else {
		printf("  SKIP  quickstart.fur not found (magic-only .fur test)\n");
	}

	sample = read_file("tests/samples/reject-ddmf.dmf", &sample_len);
	if (!sample)
		sample = read_file("/workspace/xmp-furnace/tests/samples/reject-ddmf.dmf", &sample_len);
	if (sample) {
		expect(dmf_probe_kind(sample, sample_len) == DMF_KIND_XTRACKER,
		       "sample ddmf: identified as X-Tracker");
		expect(xmp_dmf_check(sample, sample_len) == 0,
		       "sample ddmf CheckFile: REJECT");
		free(sample);
	} else {
		printf("  SKIP  reject-ddmf.dmf not found\n");
	}

	if (g_failed) {
		printf("FAILED\n");
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
