/*
 * xbased native core — achievements: RetroAchievements' rcheevos runtime run
 * over the game's memory each vblank, with the achievements read from local
 * files (no RetroAchievements account or server: Original Xbox isn't open on
 * their site yet). docs/achievements.md has the formats.
 *
 * Addresses are RetroAchievements' for the Xbox (rcheevos' console 22):
 * physical RAM, 0 to 64 MB (128 MB on a debug kit). Those of 64 MB and up
 * are read as the game's own (virtual) addresses, the 0x8xxxxxxx of
 * xbased's first achievement files among them.
 *
 *   <exe dir>/achievements/<TITLE ID>.txt        shipped
 *   <data dir>/achievements/<TITLE ID>.txt       the player's own, read after
 *                                                (the same name replaces)
 *   <data dir>/achievements/<TITLE ID>-User.txt  RetroAchievements' local
 *                                                format (their toolkit's), read
 *                                                too; written by the export
 *   <data dir>/achievements/<TITLE ID>.unlocked  unlocked, a name a line
 *   <data dir>/achievements/<TITLE ID>.notes     address notes
 *   <data dir>/achievements/<TITLE ID>.watch     the addresses watched in game
 *   achievements/badges/<badge>.png              an achievement's picture
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
#define NC_CHEEVOS_CONSOLE 22       /* RetroAchievements' Xbox (RC_CONSOLE_XBOX) */
#define NC_WATCH_MAX 8

/* Reads n (1-4) bytes at the game's virtual address va: false when unmapped. */
typedef bool (*NcCheevosRead)(uint32_t va, void *buf, unsigned n);

/* Where the files are: before the game starts (the app). */
void nc_cheevos_set_dirs(const char *exe_dir, const char *data_dir);
/* The title's achievements, notes and watches read (the XBE loaded). */
void nc_cheevos_bind(uint32_t title_id);
uint32_t nc_cheevos_title(void);
/* Each vblank, the guest stopped: unlocks what has come true. */
void nc_cheevos_frame(NcCheevosRead read);

unsigned nc_cheevos_count(void);
const char *nc_cheevos_name(unsigned i);
const char *nc_cheevos_desc(unsigned i);
const char *nc_cheevos_badge(unsigned i);   /* "" none */
unsigned nc_cheevos_points(unsigned i);
/* 1 unlocked, 0 locked, -1 no working trigger (not set up yet, or it doesn't parse) */
int nc_cheevos_state(unsigned i);
const char *nc_cheevos_error(unsigned i);   /* why ("" none) */
/* The next unlock not shown yet: its index; false when none (any thread). */
bool nc_cheevos_take_unlock(unsigned *index);
/* The achievements read again (their files edited), the unlocks kept. */
void nc_cheevos_reload(void);
void nc_cheevos_relock_all(void);
/* A new achievement in the player's file, active at once; false (err) when
 * its trigger doesn't parse or the file can't be written. */
bool nc_cheevos_add(const char *name, const char *desc, const char *trigger, unsigned points, char *err,
                    size_t en);
/* Every achievement written in RetroAchievements' local format
 * (<TITLE ID>-User.txt): their file, its path in out. */
bool nc_cheevos_export(const char *game_title, char *out, size_t n);

/* A value now: size 1, 2 or 4 bytes at a RetroAchievements address. */
bool nc_cheevos_peek(uint32_t address, unsigned size, uint32_t *value);

/* ---- address notes ---- */
const char *nc_note_get(uint32_t address);         /* "" none */
void nc_note_set(uint32_t address, const char *text);  /* "" removes */

/* ---- watched addresses (shown over the game) ---- */
bool nc_watch_add(uint32_t address, unsigned size);
void nc_watch_remove(unsigned i);
unsigned nc_watch_count(void);
bool nc_watch_get(unsigned i, uint32_t *address, unsigned *size);

/* ---- memory search (the in-game menu, the guest paused) ---- */

enum { NC_MS_EQUAL, NC_MS_CHANGED, NC_MS_UNCHANGED, NC_MS_INCREASED, NC_MS_DECREASED };

bool nc_msearch_new(unsigned size);
uint32_t nc_msearch_filter(int op, uint32_t value);
bool nc_msearch_active(void);
unsigned nc_msearch_size(void);
uint32_t nc_msearch_count(void);
/* The first max candidates: RetroAchievements addresses, the value now and at the last filter. */
uint32_t nc_msearch_list(uint32_t *address, uint32_t *now, uint32_t *prev, uint32_t max);

#ifdef __cplusplus
}
#endif

#endif
