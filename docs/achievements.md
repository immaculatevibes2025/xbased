# Achievements

xbased runs achievements with RetroAchievements' rcheevos library, from local
files: there is no RetroAchievements account or server behind them (Original
Xbox isn't one of RetroAchievements' systems). Unlocks show over the game and
are listed in the in-game menu under **Achievements**.

## Files

| File | What |
|---|---|
| `achievements\<TITLE ID>.txt` beside `xbased.exe` | shipped with xbased |
| `achievements\<TITLE ID>.txt` in the data folder | your own; an achievement of the same name replaces the shipped one |
| `achievements\<TITLE ID>.unlocked` in the data folder | what you've unlocked, a name a line |
| `achievements\found.txt` in the data folder | addresses kept from the memory search |

```
[Gargoyle Smash]
desc: Hit the gargoyle with the frying pan
trigger: d0xH0012abcd=0_0xH0012abcd=1
```

`trigger:` is a RetroAchievements condition string (the same as their
achievement editor writes). An empty one shows as **Not set up**; one that
doesn't parse says why. After editing the file, **Reload achievement file**
in the menu picks it up without restarting the game. An achievement whose
conditions are already true when the game starts won't unlock until they
have been false once (rcheevos' rule, so loading a late save doesn't unlock
everything).

The parts used most:

| Write | Means |
|---|---|
| `0xH<address>` | the 8-bit value there |
| `0x <address>` (a space) | 16-bit |
| `0xX<address>` | 32-bit |
| `d0xH<address>` | its value the frame before |
| `=` `!=` `>` `<` `>=` `<=` | compared with a number or another value |
| `_` | and (every condition true the same frame) |
| `I:0xX<pointer>_0xH<offset>` | the byte at offset past a pointer read from memory |

Addresses are the game's own, as the memory search shows them and the cheats
use. RetroAchievements' documentation has the rest:
https://docs.retroachievements.org/developer-docs/

## Finding an address with the memory search

In-game menu → **Achievements** → **Memory search**. The game is paused while
the menu is open; each filter compares memory now with what it was at the
last one.

1. Pick **Value size** (8-bit fits most flags and counters).
2. **New search** just before the thing happens, and go back to the game.
3. Make it happen, open the menu, and filter: **Changed**, **Increased**,
   **Decreased**, or **Equal to the value** (set with left and right; LB/RB
   step 100).
4. Play a little without it happening and filter **Unchanged**. Repeat until
   only a few addresses are left (they're listed once there are 2,000 or
   fewer).
5. Select one: it's copied to the clipboard as rcheevos writes it (`0xH…`)
   and saved to `found.txt`. Put it in the trigger and reload.

A value that moves around between runs (the game allocated it somewhere
else) needs a pointer: find what holds its address and use `I:`.
