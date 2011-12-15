#ifndef BUILD_NUM_H
#define BUILD_NUM_H

/* <LIC_AMD_STD>
 * Copyright (c) 2003-2005 Advanced Micro Devices, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING
 * </LIC_AMD_STD>  */
/* <CTL_AMD_STD>
 * </CTL_AMD_STD>  */
/* <DOC_AMD_STD>
 * </DOC_AMD_STD>  */

#ifdef AMD_BUILDNUM_DEFINED
#error "Caution:  You have muliple version files in your path.  Please correct this."
#endif

#define AMD_BUILDNUM_DEFINED

/* Define the following for your driver */

#define _NAME "AMD Linux LX video2linux/2 driver"
#define _MAJOR 3
#define _MINOR 2

/* This defines the current buildlevel and revision of the code */
#define _BL        01
#define _BLREV     00

#ifdef _BLREV
#define _BUILDLEVEL _x(_BL)_x(_BLREV)
#else
#define _BUILDLEVEL _x(_BL)
#endif

/* Use this macro to get the version string */
#define _VERSION_NUMBER _x(_MAJOR) "." _x(_MINOR) "." _BUILDLEVEL
#define AMD_VERSION _NAME " " _VERSION_NUMBER

/* Syntatic sugar for use above - you probably don't have to touch this */

#define _x(s) _s(s)
#define _s(s) #s

#endif
