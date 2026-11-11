# Achievements

xbased runs achievements with RetroAchievements' rcheevos library, from local
files: there is no RetroAchievements account or server behind them, since
Original Xbox isn't open on RetroAchievements yet. Everything is kept in
their formats so that achievements made here can go to them if they ever do:
the same trigger strings, their Xbox addresses (physical RAM, their console
22), their local achievement file, points and 64x64 badges.

Unlocks pop up over the game with a sound; the in-game menu's
**Achievements** lists them and holds the tools below.

## Making an achievement without writing anything

In-game menu → **Achievements** → **Memory search**. The game is paused while
the menu is open; each filter compares memory now with what it was at the
last one.

1. **Value size** (8-bit fits most counters and flags), then **New search**
   just before the thing happens. Go back to the game.
2. Make it happen, open the menu, and filter: **Changed**, **Increased**,
   **Decreased**, or **Equal to the value** (left/right sets it; LB/RB step 100).
3. Play a little without it happening and filter **Unchanged**. Repeat until
   a few addresses are left (they're listed once there are 2,000 or fewer).
4. Select one. Then:
   - **Watch it over the game**: its value stays in a corner while you play
     (up to 8). The one that moves at the right moment is the right one.
   - **Note what it is**: a label ("Conker tails") shown wherever the address is.
   - **Make an achievement of it**: name, description and points, and when it
     unlocks: *it goes down*, *it goes up*, *it becomes the value*, *it
     changes*, *it goes from one value to another*. **Create it** writes it to
     your file and it's live at once.
   - **Copy**: its address as RetroAchievements writes it.

Save states are the quickest way to test one: save just before the moment,
then load and do it again (**Lock all again** relocks everything).

The Achievements screen also sets where the pop-up shows (any corner),
whether it plays its sound, and whether watched addresses show over the game.

## Memory snapshots

Press **F9** in a game (a hotkey to change in the controls' settings, or
**Take a memory snapshot** in the Achievements menu) to save all of RAM,
numbered, in `achievements/snapshots`: `<TITLE ID>-<n>.ram`, its page map
`.map`, and when it was taken `.txt`. Take a few at moments you can name
("3 tails", "2 tails"), restart the game and take a few more. Each is 64 MB.

`tools/snapscan.py` works through them away from the game:

```
python tools/snapscan.py values   <folder> 4D530051 1=3 2=2 3=3 4=2
python tools/snapscan.py pointers <folder> 4D530051 1=0x03bb1f20 3=0x02a81f20
```

`values` finds the addresses that held each value in each snapshot;
`pointers` takes where the value was in snapshots from different runs of the
game and finds a pointer that leads to it in all of them, printed as the
trigger's `I:` part.

## Files

All in an `achievements` folder: beside `xbased.exe` for the ones that ship,
and in the data folder (the same folder when xbased can write there) for yours.

| File | What |
|---|---|
| `<TITLE ID>.txt` | achievements in xbased's format (below), as xbased ships them; an update replaces it |
| `<TITLE ID>.mine.txt` | yours, in the same format: **Make an achievement of it** writes here, and an update never touches it. One of the same name as a shipped one replaces it |
| `<TITLE ID>-User.txt` | RetroAchievements' local format (their toolkit's): read too; **Save in RetroAchievements' format** writes it |
| `<TITLE ID>.unlocked` | what you've unlocked, a name a line |
| `<TITLE ID>.notes` | address notes: `0x03bb1f20 Conker tails` |
| `<TITLE ID>.watch` | watched addresses: `0x03bb1f20 1` (the size in bytes) |
| `found.txt` | nothing now; earlier versions kept search results here |
| `snapshots/` | memory snapshots (F9) |
| `badges/<badge>.png` | an achievement's picture (64x64 as RetroAchievements', any size works); a green X until it has one |

```
[Lost Tail]
desc: Lose a life
points: 5
badge: lost-tail
trigger: 0xH03bb1f20<d0xH03bb1f20
```

`trigger:` is a RetroAchievements condition string, as their achievement
editor writes it. An empty one shows as **Not set up**; one that doesn't
parse says why. After editing a file, **Reload achievement file** picks it
up at once. An achievement already true when the game starts won't unlock
until it has been false once (rcheevos' rule: loading a late save doesn't
unlock everything).

| Write | Means |
|---|---|
| `0xH<address>` | the 8-bit value there |
| `0x <address>` (a space) | 16-bit |
| `0xX<address>` | 32-bit |
| `d0xH<address>` | its value the frame before |
| `=` `!=` `>` `<` `>=` `<=` | compared with a number or another value |
| `_` | and (every condition true the same frame) |
| `S` | or: each `S` starts a group, any one group true unlocks |
| `.300.` after a condition | true 300 frames (5 seconds), not necessarily in a row |
| `I:0xX<pointer>_0xH<offset>` | the byte at offset past a pointer read from memory |

### Addresses

Addresses are RetroAchievements' for the Xbox: physical RAM, `0x00000000` to
`0x03ffffff` (64 MB; 128 MB on a debug kit). The memory search shows them so.
Anything from 64 MB up is read as the game's own virtual address, so the
`0x8xxxxxxx` addresses of xbased's first achievement files still work; the
same RAM is `0x80000000` plus the physical address, so `0x83bb1f20` is
`0x03bb1f20`.

Pointers the game keeps in memory are virtual. Following one in a trigger
(`I:`) needs it turned physical: `I:0xX<pointer>&0x03ffffff_0xH<offset>`
for the usual `0x8xxxxxxx` ones.

RetroAchievements' documentation has the rest:
https://docs.retroachievements.org/developer-docs/

## If RetroAchievements opens the Xbox

What would remain: a hash of each game (they decide its method), logging in
and sending unlocks through rcheevos' client, and their toolkit
(RAIntegration) loaded with an emulator ID they give xbased. The
achievements, addresses, notes and badges made here carry over as they are:
**Save in RetroAchievements' format** writes the file their toolkit reads.
