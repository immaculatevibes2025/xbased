/*
 * xbased front end — an achievement's picture (cheevo_art.c): its badge,
 * achievements/badges/<badge>.png (64x64, as RetroAchievements' badges),
 * looked for in the data folder then beside the exe; a green X until it has one.
 */
#ifndef NATIVE_APP_UI_CHEEVO_ART_H
#define NATIVE_APP_UI_CHEEVO_ART_H

#include "native/app/ui/ui.h"

UiTex *cheevo_art(const char *badge);    /* never NULL once the UI is up */
void cheevo_art_free(void);
/* A screenshot's BMP (hle_present_shot) as a JPG, the BMP then removed:
 * 1 done, 0 not written whole yet, -1 failed. */
int cheevo_bmp_to_jpg(const char *bmp, const char *jpg);

#endif
