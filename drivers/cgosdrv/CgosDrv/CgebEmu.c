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

#include "CgosDrv.h"

//***************************************************************************

CGEB_BOARDINFO boardinfo={
  sizeof(CGEB_BOARDINFO),
  0,
  CGOS_BOARD_CLASS_CPU|CGOS_BOARD_CLASS_VGA,
  CGOS_BOARD_CLASS_CPU,
  "EMUL"
  };

CGEB_I2C_INFO i2cinfo={
  sizeof(CGEB_I2C_INFO),
  CGEB_I2C_TYPE_PRIMARY,
  10000,    // time per clock period in nano seconds (here 10000 for 100kHz)
  10000
  };

CGOSWDINFO wdinfo={
  sizeof(CGOSWDINFO),
  0, // dwFlags;
  0, // dwMinTimeout;
  0xffffff, // dwMaxTimeout;
  0, // dwMinDelay;
  0xffffff, // dwMaxDelay;
  0xf, // dwOpModes;         // supported operation mode mask (1<<opmode)
  3, // dwMaxStageCount;
  0xf, // dwEvents;          // supported event mask (1<<event)
  CGOS_WDOG_TYPE_BC, // dwType;
  };

CGOSVGAINFO vgainfo={
  sizeof(CGOSVGAINFO),
  CGOS_VGA_TYPE_LCD,
  0, // dwFlags;
  800, 600,  // native resolution (can be 0)
  640, 480, 16, // requested resolution (can be 0)
  255, 255 // max DAC values
  };

#define NUM_STO 2

CGEB_STORAGEAREA_INFO stoinfos[NUM_STO]={
  { sizeof(CGEB_STORAGEAREA_INFO),
    CGOS_STORAGE_AREA_EEPROM,
    0, // flags
    32, // areaSize
    0, // blockSize
    },
  { sizeof(CGEB_STORAGEAREA_INFO),
    CGOS_STORAGE_AREA_SDA,
    0, // flags
    sizeof(CGEB_SDA), // areaSize
    0, // blockSize
    },
  };

CGEB_SDA sda={
  sizeof(CGEB_SDA), // total secure data area size in bytes
  sizeof(CGEB_SDA)-4, // size of the checksummed area
  0xAA, // checksum correction byte
  "\x12\x34\x56\x78", // board serial number (BCD)
  "\0\0\0\0\0\0PN87654321", // board part number (ASCII)
  "\x11\x22\x33\x44\x55\x27",  // board EAN-13 code (BCD)
  'A',            // major product rev (ASCII)
  '0',            // minor product rev (ASCII)
  { 0x29,0x04,0x05 }, // date of manufacturing
  { 0x30,0x04,0x05 }, // date of last repair
  0,              // manufacturer ID (hex)
  "EMU1",         // project ID (ASCII)
  1,              // repair counter (hex)
  "",             // spare
  "\x40",         // board boot counter (hex)
  42,             // board running time in hours (hex)
  };

unsigned char userbytes[32]={ 1,2,3,4,0xaa };

#define NUM_IO 2

CGEB_IO_INFO ioinfos[NUM_IO]={
  {
  sizeof(CGEB_IO_INFO),
  0,
  0,
  0xff,
  0xff,
  "IO-0"
  },
  {
  sizeof(CGEB_IO_INFO),
  0,
  0,
  0xffff,
  0xffff,
  "IO-1"
  } 
  };

unsigned int iovals[NUM_IO]={ 0xa5, 0xaa55 };


//***************************************************************************

#if (!defined(_MSC_VER) && !defined(__cdecl))
#define __cdecl
#endif

void __cdecl CgebEmu(unsigned short cs, CGEBFPS *fps, unsigned short ds)
  {
  // fps->status defaults to CGEB_SUCCESS on the first step
  // so if we just return, it indicates success

  switch (fps->fct) {

    case xCgebGetCgebVersion:
      fps->rets[0]=(CGEB_VERSION_MAJOR<<24)|0x000067;
      return;

    case xCgebGetDataSize:
      fps->rets[0]=0;
      return;

    case xCgebGetSysBiosVersion:
      fps->rets[0]=0x01230000;
      return; // success

    case xCgebGetVgaBiosVersion:
    case xCgebOpen:
    case xCgebClose:
    case xCgebMapGetMem:
    case xCgebMapChanged:
//    case xCgebMapGetPorts:
    case xCgebDelayUs:
//    case xCgebGgbcReadWrite:
    case xCgebWDogTrigger:
    case xCgebWDogSetConfig:
      fps->rets[0]=0;
      return; // success

    case xCgebWDogCount:
    case xCgebVgaCount:
    case xCgebI2CCount:
      fps->rets[0]=1; // one unit available
      return;

    case xCgebBoardGetInfo:
      fps->optr=&boardinfo;
      return;

    case xCgebWDogGetInfo:
      if (fps->unit) break; // only one unit
      fps->optr=&wdinfo;
      return;

    case xCgebVgaGetInfo:
      if (fps->unit) break; // only one unit
      fps->optr=&vgainfo;
      return;

    case xCgebI2CGetInfo:
      if (fps->unit) break; // only one unit
      fps->optr=&i2cinfo;
      return;

    case xCgebI2CTransfer: {
      unsigned int i;
      dbpf((TT("EMUL: I2C lwr %d, lrd %d, %s "),fps->pars[0],fps->pars[1],
        fps->pars[2]&CG_I2C_FLAG_START?"START":"     "));
      for (i=0; i<fps->pars[0]; i++)
        dbpf((TT("%02X "),((unsigned char *)fps->iptr)[i]));
      dbpf((TT("%s\n"),
        fps->pars[2]&CG_I2C_FLAG_STOP?"STOP":"    "));
      if (fps->pars[1])
        OsaMemSet(fps->optr,(char)0xaa,fps->pars[1]); // fill read data with 0xaa
      fps->rets[0]=fps->pars[1]; // return read count
      return;
      }

    case xCgebStorageAreaCount:
      fps->rets[0]=NUM_STO;
      return;

    case xCgebStorageAreaGetInfo:
      if (fps->unit>=NUM_STO) break;
      fps->optr=&stoinfos[fps->unit];
      return;

    case xCgebStorageAreaRead:
      if (fps->unit>=NUM_STO) break;
      if (fps->pars[0]+fps->pars[1]>stoinfos[fps->unit].areaSize) {
        fps->status=CGEB_INVALID_PARAMETER;
        return;
        }
      OsaMemCpy(fps->optr,(fps->unit?(char *)&sda:(char *)userbytes)+fps->pars[0],fps->pars[1]);
      fps->rets[0]=fps->pars[1]; // return read count
      return;

    case xCgebStorageAreaWrite:
      if (fps->unit>=1) break; // allow writes only to user bytes
      if (fps->pars[0]+fps->pars[1]>stoinfos[fps->unit].areaSize) {
        fps->status=CGEB_INVALID_PARAMETER;
        return;
        }
      OsaMemCpy((fps->unit?(char *)&sda:(char *)userbytes)+fps->pars[0],fps->iptr,fps->pars[1]);
      fps->rets[0]=fps->pars[1]; // return read count
      return;


    case xCgebIOCount:
      fps->rets[0]=NUM_IO;
      return;

    case xCgebIOGetInfo:
      if (fps->unit>=NUM_IO) break;
      fps->optr=&ioinfos[fps->unit];
      return;

    case xCgebIORead:
      if (fps->unit>=NUM_IO) break;
      fps->rets[0]=iovals[fps->unit]; // return read value
      return;

    case xCgebIOWrite:
      if (fps->unit>=NUM_IO) break;
      iovals[fps->unit]=fps->pars[0];
      return;

    case xCgebIOSetDirection:
      if (fps->unit>=NUM_IO) break;
      return;

    case xCgebIOGetDirection:
      if (fps->unit>=NUM_IO) break;
      fps->rets[0]=0xff;
      return;

    }
  // a break returns an error
  fps->status=CGEB_NOT_FOUND;
  }

//***************************************************************************
