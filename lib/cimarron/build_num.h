#ifndef BUILD_NUM_H
#define BUILD_NUM_H

#ifdef AMD_BUILDNUM_DEFINED
#error "Caution:  You have muliple version files in your path.  Please correct this."
#endif

#define AMD_BUILDNUM_DEFINED

/* Define the following for your driver */

#define _NAME "AMD LX Cimarron Support Module"
#define _MAJOR 01
#define _MINOR 03

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
