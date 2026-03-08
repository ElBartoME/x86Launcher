/* watcom_compat.h - Compatibility shims to build Watcom DOS code with DJGPP.
   Include this instead of <i86.h> and <dos.h> in all source files.
*/

#ifndef __WATCOM_COMPAT_H
#define __WATCOM_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include <dos.h>      /* provides union REGS, struct SREGS, int86 */
#include <dpmi.h>
#include <go32.h>
#include <sys/nearptr.h>
#include <pc.h>       /* inportb / outportb */
#include <unistd.h>   /* getcwd / chdir */

/* __huge is meaningless in 32-bit flat memory model - just remove it */
#define __huge

/* halloc / hfree - huge memory alloc, just use malloc in 32-bit */
#define halloc(n, size)  malloc((n) * (size))
#define hfree(p)         free(p)

/* MK_FP - convert segment:offset to a flat pointer.
   In DJGPP 32-bit protected mode, conventional memory (below 1MB) is
   accessible via __djgpp_conventional_base offset.
   Usage is only for sub-1MB addresses (e.g. 0xA000 VGA segment). */
#define MK_FP(seg, off) \
    ((void*)((unsigned long)__djgpp_conventional_base + \
             ((unsigned)(seg) << 4) + (unsigned)(off)))

/* inp / outp - Watcom I/O port functions -> DJGPP equivalents */
#define inp(port)        inportb(port)
#define outp(port, val)  outportb(port, val)
#define inpw(port)       inportw(port)
#define outpw(port, val) outportw(port, val)

/* int86x - Watcom variant that takes a SREGS argument for ES.
   DJGPP's int86 doesn't need it - segment registers are handled
   automatically in protected mode. */
#define int86x(intno, in, out, sregs) int86(intno, in, out)

/* FP_OFF / FP_SEG - extract offset/segment from a far pointer.
   Not meaningful in 32-bit flat mode but needed to compile vesa.c. */
#define FP_OFF(p)  ((unsigned)(p) & 0xF)
#define FP_SEG(p)  (((unsigned long)(p) - (unsigned long)__djgpp_conventional_base) >> 4)

#endif /* __WATCOM_COMPAT_H */
