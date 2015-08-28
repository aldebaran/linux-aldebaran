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

unsigned int CgebInvoke(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned long size, unsigned long fct);
unsigned int CgebInvokePlain(CGOS_DRV_CGEB *cgeb, unsigned long fct, unsigned long *pRes);
unsigned int CgebInvokeVoid(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned long size);
unsigned long CgebInvokePar(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
    unsigned long unit, unsigned long par0, unsigned long par1,
    unsigned long par2, unsigned long par3, unsigned long *pret0, unsigned long *pret1);
unsigned long CgebInvokeIoctl(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
  CGOS_DRV_VARS *cdv);
unsigned int CgebInvokeRet(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
    unsigned long *pRes);
unsigned int CgebInvokeRetUnit(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
    unsigned long unit, unsigned long *pRes);

//***************************************************************************

void CgebClose(CGOS_DRV_CGEB *cgeb);
unsigned int CgebOpen(CGOS_DRV_VARS *cdv, unsigned char *base, unsigned long len);

//***************************************************************************

unsigned long CgebTransAddr(CGOS_DRV_CGEB *cgeb, unsigned long addr);

//***************************************************************************

#endif
