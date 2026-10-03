/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef DOSBOX_SWITCH_CONFIG_H
#define DOSBOX_SWITCH_CONFIG_H

#define VERSION "SVN"

#define C_TARGETCPU ARMV8LE

#define C_CORE_INLINE 1
#define C_FPU 1
#define C_MMX 1
#define ENVIRON_INCLUDED 1
#define ENVIRON_LINKED 1
#define HAVE_STDLIB_H 1
#define HAVE_SYS_TYPES_H 1

#define DOSBOX_PORT_SWITCH 1

#define HAVE_POWF 1
#define HAVE_DIRNAME 1
#define DB_HAVE_CLOCK_GETTIME 1

#define GCC_ATTRIBUTE(x) __attribute__((x))
#define GCC_UNLIKELY(x) __builtin_expect((x), 0)
#define GCC_LIKELY(x) __builtin_expect((x), 1)

#define INLINE inline
#define DB_FASTCALL

typedef double Real64;

typedef unsigned char      Bit8u;
typedef signed char        Bit8s;
typedef unsigned short     Bit16u;
typedef signed short       Bit16s;
typedef unsigned int       Bit32u;
typedef signed int         Bit32s;
typedef unsigned long      Bit64u;
typedef signed long        Bit64s;

#define sBit32t
#define sBit64t "l"
#define sBit32fs(a) sBit32t #a
#define sBit64fs(a) sBit64t #a

typedef Bit64u Bitu;
typedef Bit64s Bits;
#define sBitfs sBit64fs

#endif
