# Licences and notices

The MIT licence in the repository's [LICENSE](../LICENSE) covers the code of this
runtime except the components below, which keep their own terms.

## Not included: games and Macintosh system software

No game, application, ROM, System file or font is part of this repository. The
runtime reads them at run time from files the user supplies. Apple's fonts (Chicago,
Geneva, Monaco) come from the user's own System file.

## Included in the source and the builds

### Musashi (`musashi/`)

Motorola 680x0 emulator. Copyright © 1998–2002 Karl Stenerud. MIT licence; the full
text is in [`musashi/readme.txt`](musashi/readme.txt).

### SoftFloat (`musashi/softfloat/`)

IEC/IEEE floating-point arithmetic package, release 2b, by John R. Hauser, as
repackaged for MAME. Its legal notice, from
[`musashi/softfloat/README.txt`](musashi/softfloat/README.txt):

> SoftFloat was written by me, John R. Hauser. This work was made possible in part by
> the International Computer Science Institute, located at Suite 600, 1947 Center
> Street, Berkeley, California 94704. Funding was partially provided by the National
> Science Foundation under grant MIP-9311980. The original version of this code was
> written as part of a project to build a fixed-point vector processor in
> collaboration with the University of California at Berkeley, overseen by Profs.
> Nelson Morgan and John Wawrzynek.
>
> THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE. Although reasonable effort has been
> made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES RESULT IN
> INCORRECT BEHAVIOR. USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS AND ORGANIZATIONS
> WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES, COSTS, OR OTHER PROBLEMS
> THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE EFFECTIVELY INDEMNIFY JOHN HAUSER
> AND THE INTERNATIONAL COMPUTER SCIENCE INSTITUTE (possibly via similar legal warning)
> AGAINST ALL LOSSES, COSTS, OR OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS
> DUE TO THE SOFTWARE.
>
> Derivative works are acceptable, even for commercial purposes, provided that the
> minimal documentation requirements stated in the source code are satisfied.

### SDL 2 (macOS and Linux builds only)

Simple DirectMedia Layer, downloaded at build time and linked statically into the macOS
and Linux programs. Copyright © 1997–2024 Sam Lantinga. zlib licence:

> This software is provided 'as-is', without any express or implied warranty. In no
> event will the authors be held liable for any damages arising from the use of this
> software.
>
> Permission is granted to anyone to use this software for any purpose, including
> commercial applications, and to alter it and redistribute it freely, subject to the
> following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim that you
>    wrote the original software. If you use this software in a product, an
>    acknowledgment in the product documentation would be appreciated but is not
>    required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.
