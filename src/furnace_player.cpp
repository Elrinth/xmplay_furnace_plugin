#include "furnace_player.h"
#include "dmf_probe.h"

#include "engine/engine.h"
#include "ta-log.h"

#include <new>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdexcept>


/* Furnace engine expects this from main.cpp. Silent stub for embedding. */
void reportError(String what) {
	(void)what;
}


struct furnace_player {
	DivEngine *e;
	int rate;
	int chans;
	float *left;
	float *right;
	int scratch_frames;
	double duration;
	int version;
	int system_id;
	int inited;
	char title[256];
	char author[256];
	char system[128];
	char notes[1024];
	char err[256];
	char ins_tmp[128];
};

static void copy_str(char *dst, size_t cap, const String &src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	n = src.size();
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src.c_str(), n);
	dst[n] = '\0';
}

static void release_scratch(furnace_player *p)
{
	delete[] p->left;
	delete[] p->right;
	p->left = NULL;
	p->right = NULL;
	p->scratch_frames = 0;
}

static int ensure_scratch(furnace_player *p, int frames)
{
	if (frames <= 0)
		return 0;
	if (frames <= p->scratch_frames && p->left && p->right)
		return 1;
	release_scratch(p);
	p->left = new (std::nothrow) float[(size_t)frames];
	p->right = new (std::nothrow) float[(size_t)frames];
	if (!p->left || !p->right) {
		release_scratch(p);
		return 0;
	}
	p->scratch_frames = frames;
	return 1;
}

static void fill_meta(furnace_player *p)
{
	if (!p->e)
		return;
	copy_str(p->title, sizeof p->title, p->e->song.name);
	copy_str(p->author, sizeof p->author, p->e->song.author);
	copy_str(p->system, sizeof p->system, p->e->song.systemName);
	copy_str(p->notes, sizeof p->notes, p->e->song.notes);
	p->version = (int)p->e->song.version;
	p->chans = p->e->song.chans > 0 ? p->e->song.chans : 10;
	if (p->e->song.systemLen > 0)
		p->system_id = (int)p->e->song.system[0];
	if (!p->system[0])
		snprintf(p->system, sizeof p->system, "Furnace");
}

static void compute_duration(furnace_player *p)
{
	p->duration = 0.0;
	if (!p->e)
		return;
	p->e->calcSongTimestamps();
	if (p->e->curSubSong) {
		TimeMicros t = p->e->curSubSong->ts.totalTime;
		if (t.seconds >= 0)
			p->duration = t.toDouble();
		if (p->duration < 0.0 || p->duration > 86400.0)
			p->duration = 0.0;
	}
}

furnace_player *furnace_player_open(const unsigned char *data, size_t len, int rate,
                                   const char *name_hint)
{
	furnace_player *p;
	unsigned char *owned = NULL;
	DivEngine *e = NULL;
	const char *hint = name_hint;

	if (!data || len < 16)
		return NULL;
	if (!hint || !hint[0])
		hint = "song.fur";
	if (len > DMF_ZLIB_COMPRESSED_MAX && data[0] == 0x78)
		return NULL;

	p = (furnace_player *)calloc(1, sizeof *p);
	if (!p)
		return NULL;

	if (rate < 8000)
		rate = 48000;
	if (rate > 192000)
		rate = 192000;
	p->rate = rate;
	p->chans = 10;

	try {
		logLevel = LOGLEVEL_ERROR;
		/* Keep stdout quiet inside XMPlay. */
		initLog(NULL);

		e = new (std::nothrow) DivEngine();
		if (!e)
			throw std::bad_alloc();
		p->e = e;

		e->prePreInit();
		e->setConf("audioRate", rate);
		e->setConf("audioChans", 2);
		e->setConf("audioBufSize", 1024);
		e->setAudio(DIV_AUDIO_DUMMY);
		e->setConsoleMode(true, false);
		e->preInit(true);

		owned = new unsigned char[len];
		memcpy(owned, data, len);
		if (!e->load(owned, len, hint)) {
			/* load() always delete[]s the buffer. */
			owned = NULL;
			copy_str(p->err, sizeof p->err, e->getLastError());
			throw std::runtime_error(p->err[0] ? p->err : "load failed");
		}
		owned = NULL;

		if (!e->init()) {
			copy_str(p->err, sizeof p->err, e->getLastError());
			throw std::runtime_error(p->err[0] ? p->err : "init failed");
		}
		p->inited = 1;

		{
			TAAudioDesc &got = e->getAudioDescGot();
			if (got.rate >= 8000 && got.rate <= 192000)
				p->rate = (int)got.rate;
		}

		fill_meta(p);
		compute_duration(p);
		e->setRepeatPattern(false);
		e->setLoops(1);
		if (!e->play())
			throw std::runtime_error("play failed");
	} catch (const std::exception &ex) {
		if (p->err[0] == 0)
			snprintf(p->err, sizeof p->err, "%s", ex.what());
		furnace_player_close(p);
		return NULL;
	} catch (...) {
		furnace_player_close(p);
		return NULL;
	}
	return p;
}

void furnace_player_close(furnace_player *p)
{
	if (!p)
		return;
	if (p->e) {
		try {
			if (p->inited)
				p->e->quit(false);
		} catch (...) {
		}
		try {
			finishLogFile();
		} catch (...) {
		}
		delete p->e;
		p->e = NULL;
	}
	release_scratch(p);
	free(p);
}

int furnace_player_rate(const furnace_player *p)
{
	return p ? p->rate : 0;
}

int furnace_player_channels(const furnace_player *p)
{
	return p ? p->chans : 0;
}

int furnace_player_orders(const furnace_player *p)
{
	if (!p || !p->e || !p->e->curSubSong)
		return 0;
	return p->e->curSubSong->ordersLen;
}

int furnace_player_patlen(const furnace_player *p)
{
	if (!p || !p->e || !p->e->curSubSong)
		return 0;
	return p->e->curSubSong->patLen;
}

int furnace_player_version(const furnace_player *p)
{
	return p ? p->version : 0;
}

int furnace_player_system_id(const furnace_player *p)
{
	return p ? p->system_id : 0;
}

double furnace_player_duration(const furnace_player *p)
{
	return p ? p->duration : 0.0;
}

const char *furnace_player_title(const furnace_player *p)
{
	return p ? p->title : "";
}

const char *furnace_player_author(const furnace_player *p)
{
	return p ? p->author : "";
}

const char *furnace_player_system(const furnace_player *p)
{
	return p ? p->system : "";
}

const char *furnace_player_notes(const furnace_player *p)
{
	return p ? p->notes : "";
}

const char *furnace_player_error(const furnace_player *p)
{
	return p ? p->err : "null player";
}

int furnace_player_ins_count(const furnace_player *p)
{
	if (!p || !p->e)
		return 0;
	return (int)p->e->song.ins.size();
}

const char *furnace_player_ins_name(furnace_player *p, int index)
{
	if (!p || !p->e || index < 0)
		return "";
	if (index >= (int)p->e->song.ins.size())
		return "";
	DivInstrument *ins = p->e->song.ins[(size_t)index];
	if (!ins)
		return "-";
	copy_str(p->ins_tmp, sizeof p->ins_tmp, ins->name);
	return p->ins_tmp[0] ? p->ins_tmp : "-";
}

int furnace_player_process(furnace_player *p, float *interleaved, int frames)
{
	float *out[2];
	int i;

	if (!p || !p->e || !interleaved || frames <= 0)
		return 0;
	if (!p->e->isPlaying())
		return 0;
	if (!ensure_scratch(p, frames))
		return 0;

	memset(p->left, 0, (size_t)frames * sizeof(float));
	memset(p->right, 0, (size_t)frames * sizeof(float));
	out[0] = p->left;
	out[1] = p->right;
	p->e->nextBuf(NULL, out, 0, 2, (unsigned int)frames);
	for (i = 0; i < frames; ++i) {
		interleaved[i * 2] = p->left[i];
		interleaved[i * 2 + 1] = p->right[i];
	}
	return frames;
}

static int find_order_row(furnace_player *p, double target, int *order, int *row, double *actual)
{
	int o, r;
	int orders, patlen;
	double best = -1.0;
	int best_o = 0, best_r = 0;

	if (!p || !p->e || !p->e->curSubSong)
		return 0;
	orders = p->e->curSubSong->ordersLen;
	patlen = p->e->curSubSong->patLen;
	if (orders < 1 || patlen < 1)
		return 0;
	if (orders > 256)
		orders = 256;
	if (patlen > 256)
		patlen = 256;

	p->e->calcSongTimestamps();
	for (o = 0; o < orders; ++o) {
		int maxr = (int)p->e->curSubSong->ts.maxRow[o];
		int last = patlen - 1;
		if (maxr > 0 && maxr < last)
			last = maxr;
		for (r = 0; r <= last; ++r) {
			TimeMicros t = p->e->curSubSong->ts.getTimes(o, r);
			double d;
			if (t.seconds < 0)
				continue;
			d = t.toDouble();
			if (d <= target + 0.0005 && d >= best) {
				best = d;
				best_o = o;
				best_r = r;
			}
		}
	}
	if (best < 0.0)
		return 0;
	*order = best_o;
	*row = best_r;
	*actual = best;
	return 1;
}

double furnace_player_seek_ms(furnace_player *p, double ms)
{
	int order = 0, row = 0;
	double actual = 0.0;

	if (!p || !p->e)
		return -1.0;
	if (ms < 0.0)
		ms = 0.0;

	if (ms < 1.0) {
		p->e->stop();
		p->e->setOrder(0);
		if (!p->e->play())
			return -1.0;
		return 0.0;
	}

	if (!find_order_row(p, ms / 1000.0, &order, &row, &actual)) {
		/* Timestamps unavailable: only restart is supported. */
		return -1.0;
	}

	p->e->stop();
	p->e->setOrder((unsigned char)order);
	if (row <= 0) {
		if (!p->e->play())
			return -1.0;
	} else if (!p->e->playToRow(row)) {
		return -1.0;
	}
	return actual;
}
