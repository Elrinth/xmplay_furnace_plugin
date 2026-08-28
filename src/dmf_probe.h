/* Portable module-magic probe for Furnace / DefleMask / FamiTracker.
 * Used by the plugin and host-side tests. No Windows / no decoder.
 *
 * Cheap CheckFile path: magic only. DivEngine is never constructed here.
 */
#ifndef DMF_PROBE_H
#define DMF_PROBE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum dmf_kind {
	DMF_KIND_REJECT = 0,
	DMF_KIND_XTRACKER = 1,       /* DDMF — X-Tracker; leave to xmp-openmpt */
	DMF_KIND_DEFLEMASK = 2,      /* .DelekDefleMask. (raw or zlib) */
	DMF_KIND_FURNACE = 3,        /* -Furnace module- / Furnace-B */
	DMF_KIND_FURNACE_IMPORT = 4, /* FamiTracker / 0CC / Dn-FT / E-FT */
	DMF_KIND_OTHER = 5           /* TFM / FC — identified, not claimed */
};

#define DMF_PROBE_REJECT    DMF_KIND_REJECT
#define DMF_PROBE_ACCEPT    DMF_KIND_XTRACKER
#define DMF_PROBE_XTRACKER  DMF_KIND_XTRACKER
#define DMF_PROBE_DEFLEMASK DMF_KIND_DEFLEMASK
#define DMF_PROBE_FURNACE   DMF_KIND_FURNACE

#define DMF_ZLIB_COMPRESSED_MAX   ((size_t)8u * 1024u * 1024u)
#define DMF_ZLIB_UNCOMPRESSED_MAX ((size_t)32u * 1024u * 1024u)

int dmf_probe_kind(const unsigned char *buf, size_t len);
int dmf_is_zlib_header(const unsigned char *buf, size_t len);

/* 1 if this plugin claims the kind (Furnace / DefleMask / FamiTracker). */
int dmf_kind_claimed(int kind);

/* Back-compat: claimed kinds all go through Furnace. */
int dmf_kind_furnace_exclusive(int kind);

int dmf_probe_magic(const unsigned char *buf, size_t len);

int dmf_deflemask_meta(const unsigned char *buf, size_t len,
                       char *title, size_t title_cap,
                       char *author, size_t author_cap,
                       int *version, int *system);

/* CheckFile-equivalent (no decoder).
 *   1 = claim (Furnace / DefleMask / FamiTracker)
 *   0 = reject (DDMF / built-ins / TFM / FC / garbage)
 *  -1 = undecided zlib prefix (caller may slurp more)
 */
int xmp_dmf_check(const unsigned char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DMF_PROBE_H */
