/*---------------------------------------------------------------------------
 *
 * Copyright (c) 2015, congatec AG. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of 
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, 
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * The full text of the license may also be found at:        
 * http://opensource.org/licenses/GPL-2.0
 *
 *---------------------------------------------------------------------------
 */ 

//***************************************************************************

#ifndef _DRVOSA_H_
#define _DRVOSA_H_

//***************************************************************************

#ifndef cgos_cdecl
#define cgos_cdecl
#endif

//***************************************************************************

cgos_cdecl void OsaSleepms(void *ctx, unsigned int ms);
cgos_cdecl void OsaWaitus(void *ctx, unsigned int us);

//***************************************************************************

cgos_cdecl void *OsaMapAddress(unsigned int addr, unsigned int len);
cgos_cdecl void OsaUnMapAddress(void *base, unsigned int len);

//***************************************************************************

cgos_cdecl void *OsaMemAlloc(unsigned int len);
cgos_cdecl void OsaMemFree(void *p);
cgos_cdecl void OsaMemCpy(void *d, void *s, unsigned int len);
cgos_cdecl void OsaMemSet(void *d, char val, unsigned int len);

//***************************************************************************

#endif

