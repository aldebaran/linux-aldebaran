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

#ifndef _CGEBFCT_H_
#define _CGEBFCT_H_

//***************************************************************************

unsigned int CgebInvoke(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned int size, unsigned int fct);
unsigned int CgebInvokePlain(CGOS_DRV_CGEB *cgeb, unsigned int fct, unsigned int *pRes);
unsigned int CgebInvokeVoid(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned int size);
unsigned int CgebInvokePar(CGOS_DRV_CGEB *cgeb, unsigned int flags, unsigned int fct,
    unsigned int unit, unsigned int par0, unsigned int par1,
    unsigned int par2, unsigned int par3, unsigned int *pret0, unsigned int *pret1);
unsigned int CgebInvokeIoctl(CGOS_DRV_CGEB *cgeb, unsigned int flags, unsigned int fct,
  CGOS_DRV_VARS *cdv);
unsigned int CgebInvokeRet(CGOS_DRV_CGEB *cgeb, unsigned int flags, unsigned int fct,
    unsigned int *pRes);
unsigned int CgebInvokeRetUnit(CGOS_DRV_CGEB *cgeb, unsigned int flags, unsigned int fct,
    unsigned int unit, unsigned int *pRes);

//***************************************************************************

void CgebClose(CGOS_DRV_CGEB *cgeb);
unsigned int CgebOpen(CGOS_DRV_VARS *cdv, unsigned char *base, unsigned int len);

//***************************************************************************

unsigned int CgebTransAddr(CGOS_DRV_CGEB *cgeb, unsigned int addr);

//***************************************************************************

#endif
