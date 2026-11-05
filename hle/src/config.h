/* config.h -- the ONLY file you should need to edit to point this runtime at a new
 * classic-Mac application.  Every value can also be overridden on the compiler
 * command line (-DHLE_APP_NAME="\"My Game\"" etc.), which is what CMake does from
 * the cache variables of the same names.
 *
 * Everything else in src/ is a generic Mac Toolbox high-level emulation (HLE).
 * When the game you are porting needs a trap or behaviour that is missing, add it
 * to the right manager file (see docs/05-implementing-traps.md), not here.
 */
#ifndef HLE_CONFIG_H
#define HLE_CONFIG_H

/* Human-readable name: window title, dialogs, the per-user data folder name,
 * and the name the app sees for itself (CurApName / GetAppParms / OpenResFile). */
#ifndef HLE_APP_NAME
#define HLE_APP_NAME "Classic Mac App"
#endif

/* Lower-case, dash-separated name used for Linux XDG folders. */
#ifndef HLE_APP_SLUG
#define HLE_APP_SLUG "classic-mac-app"
#endif

/* Default file names the loader tries first inside the game folder.  Any file
 * whose resource fork (raw, AppleDouble or MacBinary) contains the resource type
 * HLE_APP_SIGNATURE is accepted as the application, whatever its name. */
#ifndef HLE_APP_FILE
#define HLE_APP_FILE "App.rsrc"
#endif
#ifndef HLE_SYS_FILE
#define HLE_SYS_FILE "System.rsrc"
#endif

/* A resource TYPE that only the application has (four-char code as a number).
 * 'CODE' works for almost every app (a System file has no CODE 0); a game's
 * creator-signature resource (e.g. 'Watc' for ATC) is even more specific. */
#ifndef HLE_APP_SIGNATURE
#define HLE_APP_SIGNATURE 0x434F4445u   /* 'CODE' */
#endif

/* Some apps store their settings by writing resources into their OWN resource
 * fork. We never modify the user's copy: those writes go to an overlay file
 * "<save>/<HLE_SETTINGS_OVERLAY>" that is layered over the app's fork. */
#ifndef HLE_SETTINGS_OVERLAY
#define HLE_SETTINGS_OVERLAY HLE_APP_NAME " Settings.rsrc"
#endif

/* Name of the single emulated volume the File Manager exposes. */
#ifndef HLE_VOLNAME
#define HLE_VOLNAME "HLE"
#endif

/* Prefix of every debug/test environment variable: HLE_TRACE, HLE_SCRIPT, ... */
#ifndef HLE_ENV_PREFIX
#define HLE_ENV_PREFIX "HLE_"
#endif
#define HLE_ENV(name) HLE_ENV_PREFIX name

/* CPU model Musashi emulates. 68020 runs 68000 code too; use 68030/68040 only if
 * the app checks for it (and then also fix Gestalt/SysEnvirons answers). */
#ifndef HLE_CPU
#define HLE_CPU M68K_CPU_TYPE_68020
#endif

#ifndef HLE_VERSION
#define HLE_VERSION "dev"
#endif

#endif
