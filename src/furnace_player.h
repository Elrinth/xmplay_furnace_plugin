/* Thin C wrapper around Furnace DivEngine (DefleMask .dmf).
 * DivEngine is constructed only in furnace_player_open() and destroyed
 * in furnace_player_close(). Never call these from DllMain.
 */
#ifndef FURNACE_PLAYER_H
#define FURNACE_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct furnace_player furnace_player;

/* Open a Furnace-loadable module already in memory. `data` is copied;
 * the caller retains ownership. `rate` is the requested output sample
 * rate (clamped). `name_hint` is an optional path used only for
 * extension-based imports (e.g. .tfe); may be NULL. Returns NULL on
 * failure.
 */
furnace_player *furnace_player_open(const unsigned char *data, size_t len, int rate,
                                   const char *name_hint);

void furnace_player_close(furnace_player *p);

int furnace_player_rate(const furnace_player *p);
int furnace_player_channels(const furnace_player *p);
int furnace_player_orders(const furnace_player *p);
int furnace_player_patlen(const furnace_player *p);
int furnace_player_version(const furnace_player *p);
int furnace_player_system_id(const furnace_player *p);

/* Song duration in seconds (0 if unknown). */
double furnace_player_duration(const furnace_player *p);

const char *furnace_player_title(const furnace_player *p);
const char *furnace_player_author(const furnace_player *p);
const char *furnace_player_system(const furnace_player *p);
const char *furnace_player_notes(const furnace_player *p);
const char *furnace_player_error(const furnace_player *p);

int furnace_player_ins_count(const furnace_player *p);
/* Returns a pointer valid until the next call / close. */
const char *furnace_player_ins_name(furnace_player *p, int index);

/* Render `frames` stereo frames into interleaved float LRLR...
 * Returns number of frames written (0 at end / error).
 */
int furnace_player_process(furnace_player *p, float *interleaved, int frames);

/* Seek. `ms` is milliseconds from start. Returns the actual position
 * in seconds, or -1 on failure. Position 0 always restarts.
 * Time-based seek uses Furnace song timestamps + setOrder/playToRow.
 */
double furnace_player_seek_ms(furnace_player *p, double ms);

#ifdef __cplusplus
}
#endif

#endif /* FURNACE_PLAYER_H */
