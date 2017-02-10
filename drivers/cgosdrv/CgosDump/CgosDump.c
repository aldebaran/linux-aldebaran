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
 
//***************************************************************************

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "Cgos.h"

//***************************************************************************

#ifndef TEXT
#define TEXT(s) s
#endif

//***************************************************************************

HCGOS hCgos=0;

//***************************************************************************

void report(char *s)
  {
  printf("%s\n",s);
  }

unsigned int proceed(void)
  {
  fgetc(stdin);
  return 1;
  }

//***************************************************************************

#include "CgosRprt.c"

//***************************************************************************

int main(int argc, char* argv[])
  {
  report(TEXT("CgosDump"));
  Init();
  if (!CgosBoardOpen(0,0,0,&hCgos)) {
    report(TEXT("Could not open a board"));
    }
  else
    DumpAll();
  if (hCgos) CgosBoardClose(hCgos);
  CgosLibUninitialize();
  report(TEXT("Done"));
  return 0;
  }

//***************************************************************************
