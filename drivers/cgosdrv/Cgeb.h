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

#ifndef _CGEB_H_
#define _CGEB_H_

//***************************************************************************

#pragma pack(push,4)

//***************************************************************************
//
// Current major version number
//

#define CGEB_VERSION_MAJOR 1

//***************************************************************************

#define CGEB_GET_VERSION_MAJOR(v) (((unsigned int)(v))>>24)
#define CGEB_GET_VERSION_MINOR(v) ((((unsigned int)(v))>>16)&0xff)
#define CGEB_GET_VERSION_BUILD(v) (((unsigned int)(v))&0xffff)

//***************************************************************************
//
// CGEB far pointer
//

typedef struct {
  void *off;
  unsigned short seg;
  unsigned short pad;
  } CGEBFPTR;

//***************************************************************************
//
// CGEB Low Descriptor located in 0xC0000-0xFFFFF
//

#define CGEB_LD_MAGIC "$CGEBLD$"

typedef struct {
  char magic[8];                // descriptor magic string
  unsigned short size;          // size of this descriptor
  unsigned short reserved;
  char biosName[8];             // BIOS name and revision "ppppRvvv"
  unsigned int hiDescPhysAddr; // phys addr of the high descriptor (can be 0)
                                // can also be a search hint
  } CGEB_LO_DESC;

//***************************************************************************
//
// CGEB High Descriptor located in 0xFFF00000-0xFFFFFFFF
//

#if defined(AMD64) 
#define CGEB_HD_MAGIC "$CGEBQD$"
#else
#define CGEB_HD_MAGIC "$CGEBHD$"
#endif

typedef struct {
  char magic[8];                // descriptor magic string
  unsigned short size;          // size of this descriptor
  unsigned short reserved;
  unsigned int dataSize;       // CGEB data area size
  unsigned int codeSize;       // CGEB code area size
  unsigned int entryRel;
  } CGEB_HI_DESC;

//***************************************************************************
//
// CGEB function parameter structure
//

typedef struct {
  unsigned int size;    // size of the parameter structure
  unsigned int fct;     // function number
  CGEBFPTR data;         // CGEB data area
  void *cont;    // private continuation pointer
  void *subfps;  // private sub function parameter structure pointer
  void *subfct;  // private sub function pointer
  unsigned int status;  // result codes of the function
  unsigned int unit;    // unit number or type
  unsigned int pars[4]; // input parameters
  unsigned int rets[2]; // return parameters
  void *iptr;            // input pointer
  void *optr;            // output pointer
  } CGEBFPS;

// Generic unsigned int parameter request block

#define CGEB_PARS_MAX 16

typedef struct {
  CGEBFPS fps;                // CGEB function parameter structure
  unsigned int pars[CGEB_PARS_MAX];  // parameters
  } CGEBFPS_PARS;

#define CGEB_PARS_SIZE(n) (sizeof(CGEBFPS)+(n)*sizeof(unsigned int))

//
// result status codes
//

#define CGEB_SUCCESS            0
#define CGEB_ERROR             -1
#define CGEB_INVALID_PARAMETER -2
#define CGEB_NOT_FOUND         -3
#define CGEB_READ_ERROR        -4
#define CGEB_WRITE_ERROR       -5
#define CGEB_TIMEOUT           -6

//
// continuation status codes
//

#define CGEB_NEXT               1
#define CGEB_DELAY              2
#define CGEB_NOIRQS             3

#define CGEB_DBG_STR        0x100
#define CGEB_DBG_HEX        0x101
#define CGEB_DBG_DEC        0x102

//***************************************************************************
//
// CGEB interface functions
//

#define xCgebGetCgebVersion            0
#define xCgebGetSysBiosVersion         1
#define xCgebGetVgaBiosVersion         2
#define xCgebGetDataSize               3
#define xCgebOpen                      4
#define xCgebClose                     5
#define xCgebMapGetMem                 6
#define xCgebMapChanged                7
#define xCgebMapGetPorts               8
#define xCgebDelayUs                   9
#define xCgebCgbcReadWrite            10
#define xCgebCgbcSetControl           11
#define xCgebCgbcGetInfo              12
#define xCgebCgbcHandleCommand        13
#define xCgebBoardGetInfo             14
#define xCgebBoardGetBootCounter      15
#define xCgebBoardGetRunningTimeMeter 16
#define xCgebBoardGetBootErrorLog     17
#define xCgebVgaCount                 18
#define xCgebVgaGetInfo               19
#define xCgebVgaGetContrast           20
#define xCgebVgaSetContrast           21
#define xCgebVgaGetContrastEnable     22
#define xCgebVgaSetContrastEnable     23
#define xCgebVgaGetBacklight          24
#define xCgebVgaSetBacklight          25
#define xCgebVgaGetBacklightEnable    26
#define xCgebVgaSetBacklightEnable    27
#define xCgebVgaEndDarkBoot           28
#define xCgebStorageAreaCount         29
#define xCgebStorageAreaGetInfo       30
#define xCgebStorageAreaRead          31
#define xCgebStorageAreaWrite         32
#define xCgebStorageAreaErase         33
#define xCgebStorageAreaEraseStatus   34
#define xCgebI2CCount                 35
#define xCgebI2CGetInfo               36
#define xCgebI2CGetAddrList           37
#define xCgebI2CTransfer              38
#define xCgebI2CGetFrequency          39
#define xCgebI2CSetFrequency          40
#define xCgebIOCount                  41
#define xCgebIOGetInfo                42
#define xCgebIORead                   43
#define xCgebIOWrite                  44
#define xCgebIOGetDirection           45
#define xCgebIOSetDirection           46
#define xCgebWDogCount                47
#define xCgebWDogGetInfo              48
#define xCgebWDogTrigger              49
#define xCgebWDogGetConfig            50
#define xCgebWDogSetConfig            51
#define xCgebPerformanceGetCurrent    52
#define xCgebPerformanceSetCurrent    53
#define xCgebPerformanceGetPolicyCaps 54
#define xCgebPerformanceGetPolicy     55
#define xCgebPerformanceSetPolicy     56
#define xCgebTemperatureCount         57
#define xCgebTemperatureGetInfo       58
#define xCgebTemperatureGetCurrent    59
#define xCgebTemperatureSetLimits     60
#define xCgebFanCount                 61
#define xCgebFanGetInfo               62
#define xCgebFanGetCurrent            63
#define xCgebFanSetLimits             64
#define xCgebVoltageCount             65
#define xCgebVoltageGetInfo           66
#define xCgebVoltageGetCurrent        67
#define xCgebVoltageSetLimits         68
#define xCgebStorageAreaLock          69
#define xCgebStorageAreaUnlock        70
#define xCgebStorageAreaIsLocked      71

//***************************************************************************
//
// xCgebMapGetMem
//

typedef struct {
  void *phys;     // physical address
  unsigned int size;     // size in bytes
  CGEBFPTR virt;
  } CGEB_MAP_MEM;

typedef struct {
  unsigned int count; // number of memory map entries
  CGEB_MAP_MEM entries[1];
  } CGEB_MAP_MEM_LIST;

//***************************************************************************
//
// xCgebMapGetPorts
//

typedef struct {
  unsigned short port;
  unsigned short size;
  } CGEB_MAP_PORT;

typedef struct {
  unsigned int count;
  CGEB_MAP_PORT entries[1];
  } CGEB_MAP_PORT_LIST;

#define CGEB_MAP_PORT_EXCLUSIVE 0x8000

//***************************************************************************
//
// xCgebBoardGetInfo
//

typedef struct {
  unsigned int dwSize;
  unsigned int dwFlags;
  unsigned int dwClasses;
  unsigned int dwPrimaryClass;
  char szBoard[CGOS_BOARD_MAX_SIZE_ID_STRING];
  // optional
  char szVendor[CGOS_BOARD_MAX_SIZE_ID_STRING];
  } CGEB_BOARDINFO;

//***************************************************************************
//
// xCgebStorageAreaGetInfo
//

typedef struct {
  unsigned int size;
  unsigned int type;
  unsigned int flags;
  unsigned int areaSize;
  unsigned int blockSize;
  } CGEB_STORAGEAREA_INFO;


// private storage area types

#define CGOS_STORAGE_AREA_SDA           0x00050000
#define CGOS_STORAGE_AREA_EEPROM_BIOS   0x40010000
#define CGOS_STORAGE_AREA_RAM_BIOS      0x40040000

#define CGOS_STORAGE_AREA_FLASH_STATIC  0x40020001
#define CGOS_STORAGE_AREA_FLASH_DYNAMIC 0x40020000
#define CGOS_STORAGE_AREA_FLASH_ALL     0x80020000

#define CGOS_STORAGE_AREA_MPFA          0x00060000

/*
IP (dynamic)
BootLogo (static)
CMOS Backup (dynamic)
CMOS Default Map (static)
Bootloader (static)
User Code (static)
Panel Data Table (static)
*/

#define CGOS_STORAGE_AREA_EEPROM_IP  0x01010000

//***************************************************************************
//
// I2C bus address list
//

typedef struct {
  unsigned char address;
  unsigned char flags;
  unsigned short deviceType;
  } CGEB_I2C_DEV;

typedef struct {
  unsigned int count;
  CGEB_I2C_DEV entries[1];
  } CGEB_I2C_LIST;

// Device Flags

#define CGEB_I2C_DEVFLAGS_OPTIONAL 1

// Device Types

#define CGEB_I2C_DEVTYPE_UNKNOWN        0
#define CGEB_I2C_DEVTYPE_EEPROM         1
#define CGEB_I2C_DEVTYPE_DAC            2
#define CGEB_I2C_DEVTYPE_DAC_BACKLIGHT  3
#define CGEB_I2C_DEVTYPE_DAC_CONTRAST   4

typedef struct {
  unsigned int size;
  unsigned int type;
  unsigned int frequency;
  unsigned int maxFrequency;
  } CGEB_I2C_INFO;

// I2C Types

#define CGEB_I2C_TYPE_UNKNOWN 0
#define CGEB_I2C_TYPE_PRIMARY 1
#define CGEB_I2C_TYPE_SMB     2
#define CGEB_I2C_TYPE_DDC     3
#define CGEB_I2C_TYPE_BC      4

// xCgebI2CTransfer flags

#define CG_I2C_FLAG_START   0x00080 // send START condition
#define CG_I2C_FLAG_STOP    0x00040 // send STOP condition
#define CG_I2C_FLAG_ALL_ACK 0x08000 // send ACK on all read bytes
#define CG_I2C_FLAG_ALL_NAK 0x04000 // send NAK on all read bytes

//***************************************************************************
//
// xCgebIOGetInfo
//

typedef struct {
  unsigned int size;
  unsigned short type;
  unsigned short flags;
  unsigned int outputPins;
  unsigned int inputPins;
  unsigned char *name;
  } CGEB_IO_INFO;

// IO Flags

#define CGEB_IO_ALIGNED 0
#define CGEB_IO_PACKED 1
#define CGEB_IO_PACKED_WITH_NEXT 2

//***************************************************************************

#pragma pack(pop)

//***************************************************************************

#endif
