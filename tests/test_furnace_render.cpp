/* Host-side proof: full Furnace engine loads DefleMask + .fur and
 * produces non-silent stereo PCM. Also rejects X-Tracker DDMF.
 */
#include "furnace_player.h"
#include "dmf_probe.h"

#include <math.h>
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
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long sz;
	*out_len = 0;
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	sz = ftell(f);
	if (sz < 4) { fclose(f); return NULL; }
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

static const char *first_existing(const char *a, const char *b)
{
	FILE *f = fopen(a, "rb");
	if (f) { fclose(f); return a; }
	f = fopen(b, "rb");
	if (f) { fclose(f); return b; }
	return NULL;
}

static int render_file(const char *path, const char *label, int want_seek)
{
	unsigned char *data;
	size_t len = 0;
	furnace_player *p;
	const int rate = 44100;
	const int seconds = 2;
	const int frames_total = rate * seconds;
	const int chunk = 1024;
	float *buf;
	double sumsq = 0.0, peak = 0.0;
	int got_frames = 0, n, i;
	double rms;
	int kind;

	data = read_file(path, &len);
	if (!data) {
		printf("  FAIL  %s: cannot read %s\n", label, path);
		g_failed = 1;
		return 0;
	}
	kind = dmf_probe_kind(data, len);
	printf("  info  %s: %zu bytes  probe_kind=%d\n", label, len, kind);
	if (!dmf_kind_claimed(kind)) {
		printf("  FAIL  %s: probe did not claim file\n", label);
		free(data);
		g_failed = 1;
		return 0;
	}

	p = furnace_player_open(data, len, rate, path);
	free(data);
	if (!p) {
		printf("  FAIL  %s: furnace_player_open failed\n", label);
		g_failed = 1;
		return 0;
	}
	printf("  info  %s title='%s' author='%s' system='%s'\n",
	       label, furnace_player_title(p), furnace_player_author(p),
	       furnace_player_system(p));
	printf("  info  %s rate=%d chans=%d orders=%d patlen=%d duration=%.3fs\n",
	       label, furnace_player_rate(p), furnace_player_channels(p),
	       furnace_player_orders(p), furnace_player_patlen(p),
	       furnace_player_duration(p));

	buf = (float *)malloc((size_t)chunk * 2 * sizeof(float));
	if (!buf) {
		furnace_player_close(p);
		g_failed = 1;
		return 0;
	}

	while (got_frames < frames_total) {
		int want = frames_total - got_frames;
		if (want > chunk)
			want = chunk;
		n = furnace_player_process(p, buf, want);
		if (n <= 0)
			break;
		for (i = 0; i < n * 2; ++i) {
			double s = (double)buf[i];
			double a = fabs(s);
			sumsq += s * s;
			if (a > peak)
				peak = a;
		}
		got_frames += n;
	}

	if (got_frames < 1) {
		printf("  FAIL  %s: no frames rendered\n", label);
		free(buf);
		furnace_player_close(p);
		g_failed = 1;
		return 0;
	}
	rms = sqrt(sumsq / (double)(got_frames * 2));
	printf("  info  %s rendered %d frames  rms=%.6f  peak=%.6f\n",
	       label, got_frames, rms, peak);
	if (rms < 1e-4 && peak < 1e-3) {
		/* Intro may be empty (e.g. tutorial .fur). Seek in and try again. */
		double mid = furnace_player_duration(p) * 500.0;
		double tseek;
		if (mid < 200.0)
			mid = 2000.0;
		tseek = furnace_player_seek_ms(p, mid);
		sumsq = 0.0; peak = 0.0; got_frames = 0;
		if (tseek >= 0.0) {
			while (got_frames < frames_total) {
				int want = frames_total - got_frames;
				if (want > chunk)
					want = chunk;
				n = furnace_player_process(p, buf, want);
				if (n <= 0)
					break;
				for (i = 0; i < n * 2; ++i) {
					double s = (double)buf[i];
					double a = fabs(s);
					sumsq += s * s;
					if (a > peak)
						peak = a;
				}
				got_frames += n;
			}
			rms = (got_frames > 0) ? sqrt(sumsq / (double)(got_frames * 2)) : 0.0;
			printf("  info  %s after seek(%.0fms): frames=%d rms=%.6f peak=%.6f\n",
			       label, mid, got_frames, rms, peak);
		}
		if (rms < 1e-4 && peak < 1e-3) {
			printf("  FAIL  %s: silence (rms=%.8f peak=%.8f)\n", label, rms, peak);
			free(buf);
			furnace_player_close(p);
			g_failed = 1;
			return 0;
		}
	}
	printf("  PASS  %s: non-silent audio\n", label);

	if (want_seek) {
		double t0 = furnace_player_seek_ms(p, 0.0);
		expect(t0 >= 0.0, "seek: restart (0 ms) works");
		{
			double mid = furnace_player_duration(p) * 500.0; /* half song, ms */
			double t;
			if (mid < 50.0)
				mid = 500.0;
			t = furnace_player_seek_ms(p, mid);
			if (t >= 0.0) {
				printf("  PASS  seek: mid-song seek_ms(%.0f) -> %.3fs\n", mid, t);
				n = furnace_player_process(p, buf, chunk);
				expect(n > 0, "seek: still renders after mid-song seek");
			} else {
				printf("  info  seek: mid-song timestamps unavailable (restart still works)\n");
			}
		}
	}

	free(buf);
	furnace_player_close(p);
	return 1;
}

int main(int argc, char **argv)
{
	const char *dmf;
	const char *fur;
	const char *lynx;
	const char *ddmf;
	unsigned char *data;
	size_t len = 0;
	furnace_player *p;

	(void)argc;
	(void)argv;
	printf("xmp-furnace host render / reject tests\n");

	ddmf = first_existing("tests/samples/reject-ddmf.dmf",
	                      "/workspace/xmp-furnace/tests/samples/reject-ddmf.dmf");
	if (ddmf) {
		data = read_file(ddmf, &len);
		expect(data != NULL, "ddmf: read synthetic header");
		if (data) {
			expect(dmf_probe_kind(data, len) == DMF_KIND_XTRACKER, "ddmf: kind XTRACKER");
			expect(xmp_dmf_check(data, len) == 0, "ddmf: CheckFile REJECT");
			p = furnace_player_open(data, len, 44100, ddmf);
			expect(p == NULL, "ddmf: furnace_player_open refuses X-Tracker");
			if (p)
				furnace_player_close(p);
			free(data);
		}
	} else {
		unsigned char syn[8] = { 'D', 'D', 'M', 'F', 4, 'X', 'T', 'R' };
		expect(xmp_dmf_check(syn, sizeof syn) == 0, "ddmf: synthetic CheckFile REJECT");
	}

	dmf = first_existing("tests/samples/doom-hanger.dmf",
	                     "/workspace/xmp-furnace/tests/samples/doom-hanger.dmf");
	if (dmf)
		render_file(dmf, "doom-hanger.dmf", 1);
	else {
		printf("  FAIL  doom-hanger.dmf missing\n");
		g_failed = 1;
	}

	fur = first_existing("tests/samples/quickstart.fur",
	                     "/workspace/xmp-furnace/tests/samples/quickstart.fur");
	if (fur) {
		data = read_file(fur, &len);
		if (data) {
			expect(dmf_probe_kind(data, len) == DMF_KIND_FURNACE, "quickstart.fur: kind FURNACE");
			expect(xmp_dmf_check(data, len) == 1, "quickstart.fur: CheckFile accept");
			p = furnace_player_open(data, len, 44100, fur);
			expect(p != NULL, "quickstart.fur: loads in full engine");
			if (p) {
				printf("  info  quickstart.fur title='%s' system='%s' dur=%.3fs\n",
				       furnace_player_title(p), furnace_player_system(p),
				       furnace_player_duration(p));
				expect(furnace_player_duration(p) > 0.0, "quickstart.fur: duration > 0");
				furnace_player_close(p);
			}
			free(data);
		}
	} else
		printf("  SKIP  no quickstart.fur — magic is tested in test_probe\n");

	lynx = first_existing("tests/samples/LedStorm.fur",
	                      "/workspace/xmp-furnace/tests/samples/LedStorm.fur");
	if (lynx)
		render_file(lynx, "LedStorm.fur (Lynx — non-DefleMask chip)", 0);
	else
		printf("  SKIP  no Lynx .fur (full-chip proof)\n");

	if (g_failed) {
		printf("FAILED\n");
		return 1;
	}
	printf("all render tests passed\n");
	return 0;
}
