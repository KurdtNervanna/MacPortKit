# PORT_NOTES — <Game name> <version>

> Lab notebook for this port. Agents and humans: read this first every session,
> and write findings here *as they happen*, with addresses. Newest status at the top.

## Status (update every session)

- **Date:** YYYY-MM-DD
- **Reaches:** e.g. "title → main menu; gameplay starts; crashes on level end"
- **Working on:** …
- **Next:** …
- **Baseline test:** `HLE_SCRIPT=...` → expected screenshot `docs/ref/xxx.png`

## Identity

| | |
|---|---|
| App / version | |
| Type / creator | |
| Compiler (fingerprint) | |
| CODE segments | n (largest: …) |
| Jump table entries | |
| A5 globals | bytes |
| Distinct traps | n (unimplemented at start: m) |
| Graphics | B&W / Color QD, depth, screen size |
| Sound | Sound Manager / .Sound driver / direct SoundBase |
| Input | events / GetKeys / low-mem KeyMap / VBL task |
| Code resources besides CODE | WDEF/MDEF/… |
| Files the app writes | |

## Address map (runtime ↔ Ghidra ↔ file)

- Segment load addresses (HLE_SEGLOG): …
- Ghidra layout: CODE n at 0x100000 + n*0x10000, A5 = 0x800000

## Confirmed facts (with evidence)

| Address | Name / role | Evidence |
|---|---|---|
| 0x1F1522 | sceneDrawDispatch | called once per frame (PC counter), branches on g_viewMode |

## Hypotheses log

| Date | Hypothesis | Test | Result |
|---|---|---|---|
| | "Mission ends because no enemies" | counters on enemy globals | **Disproved**: collision check fails frame 1 |

## Patches applied to game code (loader-time, switchable)

| Address | Bytes | Why | Switch |
|---|---|---|---|

## Debug switches

| Env var | Effect |
|---|---|

## Open questions

- …
