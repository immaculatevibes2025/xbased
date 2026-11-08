/*
 * xbased native core — achievements: RetroAchievements' rcheevos runtime run
 * over the game's memory each vblank, with the achievements read from a
 * local file (no RetroAchievements account or server: Original Xbox isn't
 * one of their systems). docs/achievements.md has the file's format.
 *
 *   <exe dir>/achievements/<TITLE ID>.txt    shipped
 *   <data dir>/achievements/<TITLE ID>.txt   the player's own (read after,
 *                                            an achievement of the same name
 *                                            replacing the shipped one's)
 *   <data dir>/achievements/<TITLE ID>.unlocked   those unlocked, a name a line
 *
 * Addresses are the game's own (as the cheats'): pointers read from memory
 * work with rcheevos' AddAddress.
 */
#ifndef NATIVE_CORE_CHEEVOS_H
#define NATIVE_CORE_CHEEVOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_CHEEVOS_MAX 128

/* Reads n (1-4) bytes at the game's address va: false when it isn't mapped. */
typedef bool (*NcCheevosRead)(uint32_t va, void *buf, unsigned n);

/* Where the files are: before the game starts (the app). */
void nc_cheevos_set_dirs(const char *exe_dir, const char *data_dir);
/* The title's achievements read (the XBE loaded). */
void nc_cheevos_bind(uint32_t title_id);
/* Each vblank, the guest stopped: unlocks what has come true. */
void nc_cheevos_frame(NcCheevosRead read);

unsigned nc_cheevos_count(void);
const char *nc_cheevos_name(unsigned i);
const char *nc_cheevos_desc(unsigned i);
/* 1 unlocked, 0 locked, -1 no working trigger (not set up yet, or it doesn't parse) */
int nc_cheevos_state(unsigned i);
/* why the trigger didn't parse ("" when it did) */
const char *nc_cheevos_error(unsigned i);
/* The next unlock's name not shown yet: false when none (any thread). */
bool nc_cheevos_take_unlock(char *name, size_t n);
/* The achievements read again (their file edited) and the unlocks kept. */
void nc_cheevos_reload(void);
/* Each one locked again, the .unlocked file emptied. */
void nc_cheevos_relock_all(void);

/* ---- memory search (the in-game menu, the guest paused) ---- */

enum { NC_MS_EQUAL, NC_MS_CHANGED, NC_MS_UNCHANGED, NC_MS_INCREASED, NC_MS_DECREASED };

/* A new search over all of RAM, values size bytes wide (1, 2 or 4): every
 * address a candidate, today's values the ones compared next. */
bool nc_msearch_new(unsigned size);
/* Candidates kept where the value now compares so (value: NC_MS_EQUAL's);
 * the values now are what the next filter compares with. Their count. */
uint32_t nc_msearch_filter(int op, uint32_t value);
bool nc_msearch_active(void);
unsigned nc_msearch_size(void);
uint32_t nc_msearch_count(void);
/* The first max candidates (one pass over RAM): the game's address, the
 * value now and at the last filter. Their count. */
uint32_t nc_msearch_list(uint32_t *va, uint32_t *now, uint32_t *prev, uint32_t max);

#ifdef __cplusplus
}
#endif

#endif
