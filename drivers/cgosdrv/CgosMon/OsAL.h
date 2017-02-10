/*---------------------------------------------------------------------------
 *
 * Copyright (c) 2015, congatec AG. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the BSD 2-clause license which 
 * accompanies this distribution. 
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the BSD 2-clause license for more details.
 *
 * The full text of the license may be found at:        
 * http://opensource.org/licenses/BSD-2-Clause   
 *
 *---------------------------------------------------------------------------
 */
  
#define FALSE   0
#define TRUE    1
#define a2w(x,y,z)	strncpy(x,y,z)

// WindowsCE
#if defined(UNDER_CE)
#include <windows.h>
#include <tchar.h>
#define main(x,y) WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
#undef a2w
#define a2w(x,y,z) A2W(x,y,z)
#endif

// Win32
#if defined(WIN32)
#include <windows.h>
#include <tchar.h>

#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE
#endif

#define __report(x,y) printf(x,y)
#pragma warning(disable:4996)
#endif

// Linux
#if defined(__GNUC__) && !defined(UNDER_CE)
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#define __report(x,y) fprintf(stdout,x,y)
typedef char _TCHAR;
typedef char *PTSTR;
#define Sleep(n) usleep(n*1000)
#define _tcstok strtok
#endif

