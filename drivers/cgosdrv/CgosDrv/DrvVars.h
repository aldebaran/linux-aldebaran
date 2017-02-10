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

#ifndef _DRVVARS_H_
#define _DRVVARS_H_

//***************************************************************************

#define CGOS_DRV_BOARD_MAX 4
#define CGOS_DRV_STO_MAX 16
#define CGOS_DRV_WDOG_MAX 4
#define CGOS_DRV_VGA_MAX 4
#define CGOS_DRV_IO_MAX 8

//***************************************************************************

typedef struct {
  CGEB_STORAGEAREA_INFO info;
  } CGOS_DRV_STO;

typedef struct {
  CGOSWDCONFIG config;
  unsigned long valid;
  } CGOS_DRV_WDOG;

typedef struct {
  unsigned long value;
  unsigned long valueValid;
  unsigned long enabled;
  unsigned long enabledValid;
  } CGOS_DRV_DAC;

typedef struct {
  CGOS_DRV_DAC backlight;
  CGOS_DRV_DAC contrast;
  } CGOS_DRV_VGA;

typedef struct {
  unsigned long num;
  unsigned long flags;
  unsigned long outmask;
  unsigned long inmask;
  unsigned long shift;
  unsigned long value;
  unsigned long valueValid;
  } CGOS_DRV_IO;

//***************************************************************************

typedef struct {
  unsigned char *entry;
  unsigned char *code;
  unsigned char *data;
  unsigned short ds;
  void *mapMem;
  unsigned char *hiDescStart;
  unsigned long hiDescLen;
  } CGOS_DRV_CGEB;

//***************************************************************************

typedef struct {
  CGOSBOARDINFOA info;
  unsigned long validFlags;
  unsigned long stoCount;
  CGOS_DRV_STO sto[CGOS_DRV_STO_MAX];
  unsigned long i2cCount;
  unsigned long wdogCount;
//  CGOS_DRV_WDOG wdog[CGOS_DRV_WDOG_MAX];
//  unsigned long vgaCount;
//  CGOS_DRV_VGA vga[CGOS_DRV_VGA_MAX];
//  unsigned long ioCount;
//  CGOS_DRV_IO io[CGOS_DRV_IO_MAX];
  CGOS_DRV_CGEB cgeb;
  } CGOS_DRV_BOARD;

//***************************************************************************/

typedef struct {
  // back pointer to OS specific vars
  void *osDrvVars;

  // boards
  unsigned long boardCount;
  CGOS_DRV_BOARD boards[CGOS_DRV_BOARD_MAX];

  // parameter passing
  CGOSIOCTLIN *cin;
  CGOSIOCTLOUT *cout;
  unsigned long nInBufferSize;
  unsigned long nOutBufferSize;
  unsigned long retcnt;
  unsigned long status;

  // pure data buffers
  void *pin;
  void *pout;
  unsigned long lin;
  unsigned long lout;
  // translated type
  unsigned long unit;

  // translated pointers
  CGOS_DRV_BOARD *brd;
  CGOS_DRV_STO *sto;
//  CGOS_DRV_WDOG *wdog;
//  CGOS_DRV_VGA *vga;
//  CGOS_DRV_IO *io;
//  unsigned long stotype,stosize;

  } CGOS_DRV_VARS;


//***************************************************************************

#endif
