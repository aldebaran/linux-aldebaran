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
#include "CgosBld.h"

//***************************************************************************

#define CGOS_DRIVER_MINOR 2

//***************************************************************************

#define CGOS_SUCCESS 0
#define CGOS_ERROR -1
#define CGOS_INVALID_PARAMETER -2
#define CGOS_NOT_IMPLEMENTED -3

#ifndef CGOS_INVALID_HANDLE
#define CGOS_INVALID_HANDLE ((void *)-1)
#endif

//***************************************************************************

#define INVALID_UNIT ((unsigned int)-1)

//***************************************************************************

unsigned char BCD(unsigned char b)
  {
  if (b==0xff) return 0;
  return (b>>4)*10+(b&0xf);
  }

void BCDtoStr(char *s, unsigned char *bcd, unsigned int cnt)
  {
  while (cnt--) {
    unsigned char b=*bcd++;
    *s++=((b>>4)&0xf)+'0';
    *s++=((b)&0xf)+'0';
    }
  }

void ConvertDate(CGOSTIME *cgostime, CGEB_DATE *cgebdate)
  {
  cgostime->wYear=BCD(cgebdate->year);
  if (cgostime->wYear) cgostime->wYear+=2000;
  cgostime->wMonth=BCD(cgebdate->month);
  cgostime->wDay=BCD(cgebdate->day);
  }

unsigned short ConvertPID(unsigned char c)
  {
  if (c>=127 || c<'0') return '0';
  return c;
  }

void ConvertInfo(CGOSBOARDINFOA *info, CGEB_SDA *sda)
  {
  if (!sda->cgTotalSize || sda->cgTotalSize==0xff) return; // data invalid

  OsaMemCpy(info->szBoardSub,sda->cgProjectId,4);
  ConvertDate(&info->stManufacturingDate,&sda->cgMfgDate);
  ConvertDate(&info->stLastRepairDate,&sda->cgRepairDate);
  BCDtoStr(info->szSerialNumber,sda->cgSerialNumber,sizeof(sda->cgSerialNumber));
  info->wProductRevision=(ConvertPID(sda->cgProductRevMaj)<<8)|(ConvertPID(sda->cgProductRevMin));
  {
  unsigned char *s=sda->cgPartNumber;
  unsigned int l=sizeof(sda->cgPartNumber);
  while (l && !*s) s++, l--;
  OsaMemCpy(info->szPartNumber,s,l);
  info->szPartNumber[l]=0;
  }
  BCDtoStr(info->szEAN,sda->cgEanCode,sizeof(sda->cgEanCode));
  info->dwManufacturer=sda->cgManufacturerId;
  }


unsigned int StoCountType(CGOS_DRV_BOARD *brd, unsigned int type)
  {
  unsigned int i,cnt=0;
  if (!type) {
    for (i=0; i<brd->stoCount; i++)
      if (!(brd->sto[i].info.type&0xC0000000)) cnt++;
    }
  else {
    for (i=0; i<brd->stoCount; i++)
      if (type==brd->sto[i].info.type) cnt++;
    }
  return cnt;
  }

unsigned int StoFindTypeInd(CGOS_DRV_BOARD *brd, unsigned int type, unsigned int ind, unsigned int mask)
  {
  unsigned int i;
  if (!type) {
    for (i=0; i<brd->stoCount; i++)
      if (!(brd->sto[i].info.type&0xC0000000) && !ind--) return i;
    }
  else {
    for (i=0; i<brd->stoCount; i++)
      if (type==(brd->sto[i].info.type&mask) && !ind--) return i;
    }
  return (unsigned int)-1;
  }

unsigned int StoFindType(CGOS_DRV_BOARD *brd, unsigned int type)
  {
  return StoFindTypeInd(brd,type&~0xff,type&0xff,~0xff);
  }

unsigned int StoRead(CGOS_DRV_BOARD *brd,
    unsigned int unit,
    unsigned int offset,
    unsigned char *pBytesRead, unsigned int dwLenRead,
    unsigned int *pdwBytesRead)
  {
  CGEBFPS fps;
  unsigned int ret;
  OsaMemSet(&fps,0,sizeof(fps));
  fps.unit=unit;
  fps.pars[0]=offset;
  fps.pars[1]=dwLenRead;
  fps.optr=pBytesRead;
  ret=CgebInvoke(&brd->cgeb,&fps,sizeof(fps),xCgebStorageAreaRead);
  if (pdwBytesRead) *pdwBytesRead=fps.rets[0];
  return ret;
  }


unsigned int SdaReadToInfo(CGOS_DRV_BOARD *brd)
  {
  CGEB_SDA sda;
  unsigned int unit=StoFindType(brd,CGOS_STORAGE_AREA_SDA);
  unsigned int ret;
  ret=StoRead(brd,unit,0,(unsigned char *)&sda,sizeof(sda),NULL);
  if (!ret) return ret;
  ConvertInfo(&brd->info,&sda);
  return ret;
  }

//***************************************************************************

unsigned int I2CFindTypeInd(CGOS_DRV_BOARD *brd, unsigned int type, unsigned int ind)
  {
  unsigned int i;
  CGEB_I2C_INFO *info;
  if (!type) return ind;
  for (i=0; i<brd->i2cCount; i++) {
    if (!CgebInvokeRetUnit(&brd->cgeb,4,xCgebI2CGetInfo,i,(void *)&info) || !info) break;
    if (type==(info->type<<16) && !ind--) return i;
    }
  return (unsigned int)-1;
  }

unsigned int I2CFindType(CGOS_DRV_BOARD *brd, unsigned int type)
  {
  return I2CFindTypeInd(brd,type&~0xffff,type&0xffff);
  }

//***************************************************************************

//
// I2C transfer
//

unsigned int I2CTransfer(CGOS_DRV_VARS *cdv,
    unsigned int unit, unsigned int flags,
    unsigned char *pBytesWrite, unsigned int dwLenWrite,
    unsigned char *pBytesRead, unsigned int dwLenRead,
    unsigned int *pdwBytesRead, unsigned int *pdwStatus)
  {
  CGEBFPS fps;
  unsigned int ret;
  OsaMemSet(&fps,0,sizeof(fps));
  fps.unit=unit;
  fps.pars[0]=dwLenWrite;
  fps.pars[1]=dwLenRead;
  fps.pars[2]=flags;
  fps.iptr=pBytesWrite;
  fps.optr=pBytesRead;
  ret=CgebInvoke(&cdv->brd->cgeb,&fps,sizeof(fps),xCgebI2CTransfer);
  if (pdwBytesRead) *pdwBytesRead=fps.rets[0];
  if (pdwStatus) *pdwStatus=fps.status;
//  return ret?CGOS_SUCCESS:CGOS_ERROR;
  return ret;
  }

//***************************************************************************

unsigned int zCgosSuccess(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=0;
  return CGOS_SUCCESS;
  }

unsigned int zCgosDrvGetVersion(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=(((((unsigned int)CGOS_DRIVER_MAJOR)<<8)|CGOS_DRIVER_MINOR)<<16)|CGOS_BUILD_NUMBER;
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardClose(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardCount(CGOS_DRV_VARS *cdv)
  {
  enum { dwClass,dwFlags };
  unsigned int i,cnt=0;
  if (!cdv->cin->pars[dwClass]) // no preferred class
    cnt=cdv->boardCount; // just return the total number of boards
  else { // filter requested classes
    unsigned int mask=(unsigned int)-!(cdv->cin->pars[dwFlags]&CGOS_BOARD_OPEN_FLAGS_PRIMARYONLY); // flags
    for (i=0; i<cdv->boardCount; i++)
      if ((cdv->boards[i].info.dwPrimaryClass|(cdv->boards[i].info.dwClasses&mask))&cdv->cin->pars[dwClass])
        cnt++;
    }
  cdv->cout->rets[0]=cnt;
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardOpen(CGOS_DRV_VARS *cdv)
  {
  enum { dwClass,dwFlags };
  unsigned int i,cnt=cdv->cin->type; // number within filtered classes
  if (!cdv->cin->pars[dwClass]) {  // no preferred class
    if (cnt>=cdv->boardCount) return CGOS_ERROR;
    cdv->cout->rets[0]=cnt+1; // hCgos is the board number + 1
    return CGOS_SUCCESS;
    }
  else { // filter requested classes
    unsigned int mask=(unsigned int)-!(cdv->cin->pars[dwFlags]&CGOS_BOARD_OPEN_FLAGS_PRIMARYONLY);
    for (i=0; i<cdv->boardCount; i++)
      if ((cdv->boards[i].info.dwPrimaryClass|(cdv->boards[i].info.dwClasses&mask))&cdv->cin->pars[dwClass])
        if (!cnt--) {
          cdv->cout->rets[0]=i+1; // hCgos is the board number + 1
          return CGOS_SUCCESS;
          }
    }
  return CGOS_ERROR;
  }

unsigned int zCgosBoardOpenByNameA(CGOS_DRV_VARS *cdv)
  {
  unsigned int i;
  if (cdv->lin<4) return CGOS_ERROR; // REVIEW
  for (i=0; i<cdv->boardCount; i++)
    // REVIEW this assumes that board names are always 4 chars, we don't have a strcmp yet!
    if (*(unsigned int *)cdv->boards[i].info.szBoard==*(unsigned int *)cdv->pin) {
      cdv->cout->rets[0]=i+1; // hCgos is the board number + 1
      return CGOS_SUCCESS;
      }
  return CGOS_ERROR;
  }

unsigned int zCgosBoardGetNameA(CGOS_DRV_VARS *cdv)
  {
  // should be a OsaStrNCpy
  OsaMemCpy(cdv->pout,cdv->brd->info.szBoard,cdv->lout);
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardGetInfoA(CGOS_DRV_VARS *cdv)
  {
  OsaMemCpy(cdv->pout,&cdv->brd->info,cdv->lout);
  return CGOS_SUCCESS;
  }
/*
unsigned int zCgosBoardGetBootCounter(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardGetRunningTimeMeter(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardGetOption(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardSetOption(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosBoardGetBootErrorLog(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaGetContrast(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaSetContrast(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaGetContrastEnable(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaSetContrastEnable(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaGetBacklight(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaSetBacklight(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaGetBacklightEnable(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaSetBacklightEnable(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVgaEndDarkBoot(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int zCgosStorageAreaCount(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=StoCountType(cdv->brd,cdv->cin->type);
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaType(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=cdv->sto->info.type&0x00ff0000;
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaSize(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=cdv->sto->info.areaSize;
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaBlockSize(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=cdv->sto->info.blockSize;
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosStorageAreaRead(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaWrite(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaErase(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosStorageAreaEraseStatus(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/
unsigned int zCgosI2CCount(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=cdv->brd->i2cCount;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CType(CGOS_DRV_VARS *cdv)
  {
  CGEB_I2C_INFO *info;
  if (!CgebInvokeRetUnit(&cdv->brd->cgeb,4,xCgebI2CGetInfo,cdv->unit,(void *)&info) || !info) return CGOS_ERROR;
  cdv->cout->rets[0]=info->type<<16;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CIsAvailable(CGOS_DRV_VARS *cdv)
  {
  // TODO: we need to walk thru the types here
  cdv->cout->rets[0]=cdv->unit<cdv->brd->i2cCount;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CRead(CGOS_DRV_VARS *cdv)
  {
  unsigned int stat;
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START|CG_I2C_FLAG_STOP,
    (unsigned char *)&cdv->cin->pars[0],1,cdv->pout,cdv->lout,&cdv->retcnt,&stat)) return stat;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CWrite(CGOS_DRV_VARS *cdv)
  {
  unsigned int stat;
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START,
      (unsigned char *)&cdv->cin->pars[0],1,NULL,0,NULL,&stat)) return stat;
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_STOP,
      (unsigned char *)(cdv->cin+1),cdv->lin,NULL,0,NULL,&stat)) return stat;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CReadRegister(CGOS_DRV_VARS *cdv)
  {
  unsigned int stat;
  enum { bAddr,wReg };
  unsigned char b[2];
  b[0]=(unsigned char)(cdv->cin->pars[bAddr]|((cdv->cin->pars[wReg]>>7)&0x0e))&0xfe; // addr
  b[1]=(unsigned char)cdv->cin->pars[wReg]; // register
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START,
      b,2,NULL,0,NULL,&stat)) return stat;
  // read value
  b[0]|=1;
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START|CG_I2C_FLAG_STOP,
    b,1,b+1,1,NULL,&stat)) return stat;
  cdv->cout->rets[0]=b[1];
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CWriteRegister(CGOS_DRV_VARS *cdv)
  {
  unsigned int stat;
  enum { bAddr,wReg,bData };
  unsigned char b[3];
  b[0]=(unsigned char)(cdv->cin->pars[bAddr]|((cdv->cin->pars[wReg]>>7)&0x0e))&0xfe; // addr
  b[1]=(unsigned char)cdv->cin->pars[wReg]; // register
  b[2]=(unsigned char)cdv->cin->pars[bData]; // value
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START|CG_I2C_FLAG_STOP,
      b,3,NULL,0,NULL,&stat)) return stat;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CWriteReadCombined(CGOS_DRV_VARS *cdv)
  {
  unsigned int stat;
  unsigned char addr=(unsigned char)cdv->cin->pars[0]&0xfe; // addr
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START,
      &addr,1,NULL,0,NULL,&stat)) return stat;
  if (!I2CTransfer(cdv,cdv->cin->type,0,
      (unsigned char *)(cdv->cin+1),cdv->lin,NULL,0,NULL,&stat)) return stat;
  // read part
  addr|=1;
  if (!I2CTransfer(cdv,cdv->cin->type,CG_I2C_FLAG_START|CG_I2C_FLAG_STOP,
    &addr,1,cdv->pout,cdv->lout,&cdv->retcnt,&stat)) return stat;
  return CGOS_SUCCESS;
  }

unsigned int zCgosI2CGetMaxFrequency(CGOS_DRV_VARS *cdv)
  {
  CGEB_I2C_INFO *info;
  if (!CgebInvokeRetUnit(&cdv->brd->cgeb,4,xCgebI2CGetInfo,cdv->unit,(void *)&info) || !info) return CGOS_ERROR;
  cdv->cout->rets[0]=info->maxFrequency;
  return CGOS_SUCCESS;
  }


unsigned int IoGetInfo(CGOS_DRV_VARS *cdv, CGEB_IO_INFO **info)
  {
  if (!CgebInvokeRetUnit(&cdv->brd->cgeb,4,xCgebIOGetInfo,cdv->unit,(void *)info) || !*info) return 0;
  return 1;
  }

/*

unsigned int zCgosIOCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int zCgosIOIsAvailable(CGOS_DRV_VARS *cdv)
  {
  unsigned int dw=0;
  CgebInvokePlain(&cdv->brd->cgeb,xCgebIOCount,&dw);
  cdv->cout->rets[0]=cdv->unit<dw;
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosIORead(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosIOWrite(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int zCgosIOXorAndXor(CGOS_DRV_VARS *cdv)
  {
  unsigned int dw;
  if (!CgebInvokeRetUnit(&cdv->brd->cgeb,1,xCgebIORead,cdv->unit,&dw)) return CGOS_ERROR;
  dw^=cdv->cin->pars[0];
  dw&=cdv->cin->pars[1];
  dw^=cdv->cin->pars[2];
  if (!CgebInvokePar(&cdv->brd->cgeb,0,xCgebIOWrite,cdv->unit,dw,0,0,0,NULL,NULL)) return CGOS_ERROR;
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosIOGetDirection(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosIOSetDirection(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int zCgosIOGetDirectionCaps(CGOS_DRV_VARS *cdv)
  {
  CGEB_IO_INFO *info;
  if (!IoGetInfo(cdv,&info)) return CGOS_ERROR;
  cdv->cout->rets[0]=info->inputPins;
  cdv->cout->rets[1]=info->outputPins;
  return CGOS_SUCCESS;
  }

unsigned int zCgosIOGetNameA(CGOS_DRV_VARS *cdv)
  {
  CGEB_IO_INFO *info;
  if (!IoGetInfo(cdv,&info)) return CGOS_ERROR;
  // should be a OsaStrNCpy
  OsaMemCpy(cdv->pout,info->name?info->name:(unsigned char *)"",cdv->lout);
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosWDogCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int zCgosWDogIsAvailable(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=cdv->unit<cdv->brd->wdogCount;
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosWDogTrigger(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosWDogGetTriggerCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosWDogSetTriggerCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosWDogGetConfigStruct(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosWDogSetConfigStruct(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/

unsigned int WdSet(CGOS_DRV_VARS *cdv, CGOSWDCONFIG *wdi)
  {
  CGEBFPS fps;
  OsaMemSet(&fps,0,sizeof(fps));
  fps.unit=cdv->cin->type;
  fps.iptr=wdi;
  return CgebInvoke(&cdv->brd->cgeb,&fps,sizeof(fps),xCgebWDogSetConfig);
  }

unsigned int zCgosWDogSetConfig(CGOS_DRV_VARS *cdv)
  {
  enum { dwTimeout, dwDelay, dwMode };
  CGOSWDCONFIG wdi;
  OsaMemSet(&wdi,0,sizeof(wdi));
  wdi.dwSize=sizeof(wdi);
  wdi.dwTimeout=cdv->cin->pars[dwTimeout];
  wdi.dwDelay=cdv->cin->pars[dwDelay];
  if (!wdi.dwTimeout && !wdi.dwDelay) {
    wdi.dwMode=CGOS_WDOG_MODE_STAGED;
    wdi.dwOpMode=CGOS_WDOG_OPMODE_DISABLED;
    }
  else {
    wdi.dwMode=cdv->cin->pars[dwMode]; // |CGOS_WDOG_MODE_STAGED;
    wdi.dwOpMode=CGOS_WDOG_OPMODE_SINGLE_EVENT;
    wdi.dwStageCount=1;
    wdi.stStages[0].dwEvent=(wdi.dwMode&CGOS_WDOG_MODE_RESTART_OS)?CGOS_WDOG_EVENT_INT:CGOS_WDOG_EVENT_RST;
    wdi.stStages[0].dwTimeout=cdv->cin->pars[dwTimeout];
    }
  return WdSet(cdv,&wdi)?CGOS_SUCCESS:CGOS_ERROR;
  }

unsigned int zCgosWDogDisable(CGOS_DRV_VARS *cdv)
  {
  CGOSWDCONFIG wdi;
  OsaMemSet(&wdi,0,sizeof(wdi));
  wdi.dwSize=sizeof(wdi);
  wdi.dwMode=CGOS_WDOG_MODE_STAGED;
  wdi.dwOpMode=CGOS_WDOG_OPMODE_DISABLED;
  return WdSet(cdv,&wdi)?CGOS_SUCCESS:CGOS_ERROR;
  }

/*
unsigned int zCgosPerformanceGetCurrent(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosPerformanceSetCurrent(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosPerformanceGetPolicyCaps(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosPerformanceGetPolicy(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosPerformanceSetPolicy(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosTemperatureCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosTemperatureGetInfo(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosTemperatureGetCurrent(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosTemperatureSetLimits(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosFanCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosFanGetInfo(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosFanGetCurrent(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosFanSetLimits(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVoltageCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVoltageGetInfo(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVoltageGetCurrent(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

unsigned int zCgosVoltageSetLimits(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }
*/
unsigned int zCgosCgeb(CGOS_DRV_VARS *cdv)
  {
  if (cdv->lout<cdv->lin) return CGOS_ERROR;
  cdv->retcnt=cdv->lout;
  OsaMemCpy((char *)(cdv->cout+1),(char *)(cdv->cin+1),cdv->lin);
  CgebInvokeVoid(&cdv->brd->cgeb,(CGEBFPS *)(cdv->cout+1),cdv->lout);
  return CGOS_SUCCESS;
  }

unsigned int zCgosCgebTransAddr(CGOS_DRV_VARS *cdv)
  {
  cdv->cout->rets[0]=CgebTransAddr(&cdv->brd->cgeb,cdv->cin->pars[0]);
  return CGOS_SUCCESS;
  }

/*
unsigned int zCgosVgaCount(CGOS_DRV_VARS *cdv)
  {
  return CGOS_SUCCESS;
  }

*/

unsigned int dbpflevel=0;

unsigned int zCgosCgebDbgLevel(CGOS_DRV_VARS *cdv)
  {
  dbpflevel=cdv->cin->pars[0];
  return CGOS_SUCCESS;
  }

//***************************************************************************

#define CGOS_DRV_FCT_MAX (sizeof(DrvFcts)/sizeof(*DrvFcts))

#define CDFF_x    0x0000
#define CDFF_     0x0000
#define CDFF_NOH  0x0001
#define CDFF_TYP  0x0002
#define CDFF_BRD  0x0000
#define CDFF_VGA  0x0200
#define CDFF_STO  0x0400
#define CDFF_I2C  0x0800
#define CDFF_IO   0x1000
#define CDFF_WD   0x2000

#define CDFF_MASK 0xFF00

typedef struct {
  unsigned int xfct;
  unsigned int  (*fct)(CGOS_DRV_VARS *cdv);
  unsigned int minin;
  unsigned int minout;
  unsigned int flags;
  unsigned int cgebfct;
  unsigned int cgebflags;
  } CGOS_DRV_FCT;

//***************************************************************************

#define xCgebWDogGetConfigStruct xCgebWDogGetConfig
#define xCgebWDogSetConfigStruct xCgebWDogSetConfig

// driver function call dispatch
#define X(name) zCgos##name
#define o(name) NULL
#define B(name) NULL
#define Z(name) zCgosSuccess

// cgeb function number dispatch
#define X2(name) 0
#define o2(name) 0
#define B2(name) xCgeb##name
#define Z2(name) 0

// the first parameter describes how the call will be dispatched:
//   o: just return not implemented
//   Z: just return success and zero results
//   X: dispatch to the drivers zCgos* function with the base name
//   B: dispatch to the xCgeb* function with the base name
// the second parameter is the base name
// the third and fourth parameters are 2 flags for parameter checking

#define df(oX,name,f0,f1,mi,mo,cgebflags) { xCgos##name,oX(name),mi,mo,CDFF_##f0|CDFF_##f1,oX##2(name),cgebflags }

CGOS_DRV_FCT DrvFcts[]={
  df(X,DrvGetVersion           ,NOH,x  ,0,0,0),
  df(X,BoardClose              ,BRD,x  ,0,0,0),
  df(X,BoardCount              ,NOH,x  ,0,0,0),
  df(X,BoardOpen               ,NOH,x  ,0,0,0),
  df(X,BoardOpenByNameA        ,NOH,x  ,0,0,0),
  df(X,BoardGetNameA           ,BRD,x  ,0,CGOS_BOARD_MAX_LEN_ID_STRING,0),
  df(X,BoardGetInfoA           ,BRD,x  ,0,sizeof(CGOSBOARDINFOA),0),
  df(B,BoardGetBootCounter     ,BRD,x  ,0,0,0),
  df(B,BoardGetRunningTimeMeter,BRD,x  ,0,0,0),
  df(o,BoardGetOption          ,BRD,x  ,0,0,0),
  df(o,BoardSetOption          ,BRD,x  ,0,0,0),
  df(o,BoardGetBootErrorLog    ,BRD,x  ,0,0,0),
  df(B,VgaCount                ,x  ,x  ,0,0,0),
  df(B,VgaGetContrast          ,VGA,x  ,0,0,0),
  df(B,VgaSetContrast          ,VGA,x  ,0,0,0),
  df(B,VgaGetContrastEnable    ,VGA,x  ,0,0,0),
  df(B,VgaSetContrastEnable    ,VGA,x  ,0,0,0),
  df(B,VgaGetBacklight         ,VGA,x  ,0,0,0),
  df(B,VgaSetBacklight         ,VGA,x  ,0,0,0),
  df(B,VgaGetBacklightEnable   ,VGA,x  ,0,0,0),
  df(B,VgaSetBacklightEnable   ,VGA,x  ,0,0,0),
  df(B,VgaEndDarkBoot          ,VGA,x  ,0,0,0),
  df(B,VgaGetInfo              ,VGA,x  ,0,sizeof(CGOSVGAINFO),0),
  df(X,StorageAreaCount        ,x  ,x  ,0,0,0), // no STO type checks
  df(X,StorageAreaType         ,STO,x  ,0,0,0),
  df(X,StorageAreaSize         ,STO,x  ,0,0,0),
  df(X,StorageAreaBlockSize    ,STO,x  ,0,0,0),
  df(B,StorageAreaRead         ,STO,x  ,0,0,32),
  df(B,StorageAreaWrite        ,STO,x  ,0,0,0),
  df(B,StorageAreaErase        ,STO,x  ,0,0,0),
  df(B,StorageAreaEraseStatus  ,STO,x  ,0,0,0),
  df(X,I2CCount                ,x  ,x  ,0,0,0),
  df(X,I2CType                 ,I2C,x  ,0,0,0),
  df(X,I2CIsAvailable          ,I2C,x  ,0,0,0),
  df(X,I2CRead                 ,I2C,x  ,0,0,32),
  df(X,I2CWrite                ,I2C,x  ,0,0,0),
  df(X,I2CReadRegister         ,I2C,x  ,0,0,0),
  df(X,I2CWriteRegister        ,I2C,x  ,0,0,0),
  df(X,I2CWriteReadCombined    ,I2C,x  ,0,0,0),
  df(B,IOCount                 ,x  ,x  ,0,0,0),
  df(X,IOIsAvailable           ,IO ,x  ,0,0,0),
  df(B,IORead                  ,IO ,x  ,0,0,0),
  df(B,IOWrite                 ,IO ,x  ,0,0,0),
  df(X,IOXorAndXor             ,IO ,x  ,0,0,0),
  df(B,IOGetDirection          ,IO ,x  ,0,0,0),
  df(B,IOSetDirection          ,IO ,x  ,0,0,0),
  df(X,IOGetDirectionCaps      ,IO ,x  ,0,0,0),
  df(X,IOGetNameA              ,IO ,x  ,0,0,0),
  df(B,WDogCount               ,x  ,x  ,0,0,0),
  df(X,WDogIsAvailable         ,WD ,x  ,0,0,0),
  df(B,WDogTrigger             ,WD ,x  ,0,0,0),
  df(o,WDogGetTriggerCount     ,WD ,x  ,0,0,0),
  df(o,WDogSetTriggerCount     ,WD ,x  ,0,0,0),
  df(B,WDogGetConfigStruct     ,WD ,x  ,0,sizeof(unsigned int)*4,16), // first 4 pars of CGOSWDCONFIG are mandatory
  df(B,WDogSetConfigStruct     ,WD ,x  ,sizeof(unsigned int)*4,0,0),
  df(X,WDogSetConfig           ,WD ,x  ,0,0,0),
  df(X,WDogDisable             ,WD ,x  ,0,0,0),
  df(B,WDogGetInfo             ,WD ,x  ,0,sizeof(CGOSWDINFO),0),
  df(o,PerformanceGetCurrent   ,x  ,x  ,0,0,0),
  df(o,PerformanceSetCurrent   ,x  ,x  ,0,0,0),
  df(o,PerformanceGetPolicyCaps,x  ,x  ,0,0,0),
  df(o,PerformanceGetPolicy    ,x  ,x  ,0,0,0),
  df(o,PerformanceSetPolicy    ,x  ,x  ,0,0,0),
  df(B,TemperatureCount        ,x  ,x  ,0,0,0),
  df(B,TemperatureGetInfo      ,x  ,x  ,0,sizeof(CGOSTEMPERATUREINFO),0),
  df(B,TemperatureGetCurrent   ,x  ,x  ,0,0,0),
  df(B,TemperatureSetLimits    ,x  ,x  ,sizeof(CGOSTEMPERATUREINFO),0,0),
  df(B,FanCount                ,x  ,x  ,0,0,0),
  df(B,FanGetInfo              ,x  ,x  ,0,sizeof(CGOSFANINFO),0),
  df(B,FanGetCurrent           ,x  ,x  ,0,0,0),
  df(B,FanSetLimits            ,x  ,x  ,sizeof(CGOSFANINFO),0,0),
  df(B,VoltageCount            ,x  ,x  ,0,0,0),
  df(B,VoltageGetInfo          ,x  ,x  ,0,sizeof(CGOSVOLTAGEINFO),0),
  df(B,VoltageGetCurrent       ,x  ,x  ,0,0,0),
  df(B,VoltageSetLimits        ,x  ,x  ,sizeof(CGOSVOLTAGEINFO),0,0),
  df(X,Cgeb                    ,x  ,x  ,0,0,0),
  df(X,CgebTransAddr           ,x  ,x  ,0,0,0),
  df(X,CgebDbgLevel            ,NOH,x  ,0,0,0),
  df(B,CgbcGetInfo             ,x  ,x  ,0,sizeof(CGOSBCINFO),0),
  df(B,CgbcSetControl          ,x  ,x  ,0,0,0),
  df(B,CgbcReadWrite           ,x  ,x  ,0,0,0),
  df(B,CgbcHandleCommand       ,x  ,x  ,0,0,128),
  df(B,StorageAreaLock         ,STO,x  ,0,0,0),
  df(B,StorageAreaUnlock       ,STO,x  ,0,0,0),
  df(B,StorageAreaIsLocked     ,STO,x  ,0,0,0),
  df(X,I2CGetMaxFrequency      ,I2C,x  ,0,0,0),
  df(B,I2CGetFrequency         ,I2C,x  ,0,0,0),
  df(B,I2CSetFrequency         ,I2C,x  ,0,0,0),
  };

#undef X
#undef o
#undef B
#undef X2
#undef o2
#undef B2
#undef df

//***************************************************************************

unsigned int UlaDeviceIoControl(void *hDriver, unsigned int dwIoControlCode,
    void *pInBuffer, unsigned int nInBufferSize,
    void *pOutBuffer, unsigned int nOutBufferSize,
    unsigned int *pBytesReturned)
  {
  CGOS_DRV_VARS *cdv=hDriver;
  CGOSIOCTLIN *cin;
  CGOSIOCTLOUT *cout;
  CGOS_DRV_FCT *df;
  unsigned int dff;
  if (pBytesReturned) *pBytesReturned=0;

  // check basic ioctl pars
  if (!cdv || cdv==CGOS_INVALID_HANDLE) return CGOS_INVALID_PARAMETER;
  if (dwIoControlCode!=CGOS_IOCTL) return CGOS_NOT_IMPLEMENTED;
  if (nInBufferSize<sizeof(CGOSIOCTLIN)) return CGOS_INVALID_PARAMETER;
  if (nOutBufferSize<sizeof(CGOSIOCTLOUT)) return CGOS_INVALID_PARAMETER;
  if (!pInBuffer) return CGOS_INVALID_PARAMETER;
  if (!pOutBuffer) return CGOS_INVALID_PARAMETER;

  // set up cgos in and out par structures
  cdv->cin=cin=(CGOSIOCTLIN *)pInBuffer;
  cdv->cout=cout=(CGOSIOCTLOUT *)pOutBuffer;
  cdv->nInBufferSize=nInBufferSize;
  cdv->nOutBufferSize=nOutBufferSize;
  cdv->status=0;

  dbpf((TT("CGOS: IOCT: ->  sz %02X, fct %02X, hdl %X, typ %X, p(%X,%X,%X,%X)\n"),
    nInBufferSize,cin->fct,cin->handle,cin->type,cin->pars[0],cin->pars[1],cin->pars[2],cin->pars[3]));

  // get fct descriptor from fct number in cin
  if (cin->fct>=CGOS_DRV_FCT_MAX) return CGOS_NOT_IMPLEMENTED;
  df=DrvFcts+cin->fct;

  // REVIEW: this is just a sanity check
  if (df->xfct!=cin->fct) return CGOS_NOT_IMPLEMENTED;

  // check additional parameter requirements
  if (nInBufferSize<sizeof(CGOSIOCTLIN)+df->minin) return CGOS_INVALID_PARAMETER;
  if (nOutBufferSize<sizeof(CGOSIOCTLOUT)+df->minout) return CGOS_INVALID_PARAMETER;
  cdv->retcnt=df->minout;

  // pure data buffers
  cdv->pin=cin+1;
  cdv->pout=cout+1;
  cdv->lin=nInBufferSize-sizeof(CGOSIOCTLIN);
  cdv->lout=nOutBufferSize-sizeof(CGOSIOCTLOUT);

  // enforce driver function flags and convert parameters
  dff=df->flags;
  if (!(dff&CDFF_NOH)) { // check handle
    if (!cin->handle || cin->handle>cdv->boardCount) return CGOS_INVALID_PARAMETER;
    cdv->brd=cdv->boards+cin->handle-1;
    }
  if (dff&CDFF_TYP) { // check type
    if (cin->type) return CGOS_INVALID_PARAMETER; // type must be 0
    }
  cdv->unit=cin->type; // defaults to untranslated type
  switch (dff&CDFF_MASK) {
    case CDFF_STO:
      if (!cdv->brd->stoCount) return CGOS_INVALID_PARAMETER;
      cdv->unit=StoFindType(cdv->brd,cin->type);
      if (cdv->unit==(unsigned int)-1) return CGOS_INVALID_PARAMETER;
      cdv->sto=cdv->brd->sto+cdv->unit;
      break;
    case CDFF_I2C:
      if (!cdv->brd->i2cCount) return CGOS_INVALID_PARAMETER;
      cdv->unit=I2CFindType(cdv->brd,cin->type);
      if (cdv->unit==(unsigned int)-1) return CGOS_INVALID_PARAMETER;
      break;
#if 0
    case CDFF_WD:
      if (cin->type) return CGOS_INVALID_PARAMETER; // only one supported
      cdv->wdog=cdv->brd->wdog+cin->type;
      break;
    case CDFF_VGA:
      if (cin->type) return CGOS_INVALID_PARAMETER; // only one supported
      cdv->vga=cdv->brd->vga+cin->type;
      break;
    case CDFF_I2C:
      if (cin->type>cdv->brd->i2cCount) return CGOS_INVALID_PARAMETER;
      break;
    case CDFF_IO:
      if (cin->type>cdv->brd->ioCount) return CGOS_INVALID_PARAMETER;
      cdv->io=cdv->brd->io+cin->type;
      break;
#endif
    }
// the input and output buffer might point to the same physical buffer
// so we must always evaluate all input parameters before writing to the
// output buffers.
//  cout->rets[0]=0;
//  cout->rets[1]=0;
  if (df->fct) {
    cout->status=(*df->fct)(cdv);
    }
  else if (df->cgebfct) {
    if (!CgebInvokeIoctl(&cdv->brd->cgeb,df->cgebflags,df->cgebfct,cdv))
      cout->status=cdv->status?cdv->status:CGOS_ERROR;
//      return CGOS_ERROR;
    else
      cout->status=CGOS_SUCCESS;
    }
  else {
    cout->status=CGOS_NOT_IMPLEMENTED;
    }

  cdv->retcnt+=sizeof(CGOSIOCTLOUT);
  dbpf((TT("CGOS:  <- IOCT: sz %02X, cnt %02X, sta %X,        p(%X,%X)\n"),
    nOutBufferSize,cdv->retcnt,cout->status,cout->rets[0],cout->rets[1]));

  if (pBytesReturned) *pBytesReturned=cdv->retcnt;
  return CGOS_SUCCESS;
  }

//***************************************************************************

#if (!defined(_MSC_VER) && !defined(__cdecl))
#define __cdecl
#endif

void __cdecl CgebEmu(unsigned short cs, CGEBFPS *fps, unsigned short ds);

void *UlaOpenDriver(unsigned long reserved)
  {
  CGOS_DRV_VARS *cdv;
  //  unsigned int basenibbles,baseaddr;
  unsigned long basenibbles,baseaddr;
  cdv=OsaMemAlloc(sizeof(*cdv));
  if (!cdv) return NULL;
  OsaMemSet(cdv,0,sizeof(*cdv)); // clears boardCount;
  cdv->osDrvVars=(void *)reserved;
  for (basenibbles=0xfedc0; (baseaddr=basenibbles&0xf0000); basenibbles<<=4)
    if (CgebOpen(cdv,(void *)baseaddr,0x10000))
      return cdv;
#ifdef CGEBEMU
  dbpf((TT("CGOS: >>> WARNING: REAL CGEB NOT FOUND! USING EMULATOR! <<<\n")));
  CgebOpen(cdv,(unsigned char *)CgebEmu,0);
#endif
  return cdv;
  }

void UlaCloseDriver(void *hDriver)
  {
  CGOS_DRV_VARS *cdv=hDriver;
  if (!cdv) return;
  while (cdv->boardCount)
    CgebClose(&cdv->boards[--cdv->boardCount].cgeb);
  OsaMemFree(cdv);
  }

unsigned int UlaGetBoardCount(void *hDriver)
  {
  CGOS_DRV_VARS *cdv=hDriver;
  return cdv->boardCount;
  }

unsigned char *UlaGetBoardName(void *hDriver, unsigned int Index)
  {
  CGOS_DRV_VARS *cdv=hDriver;
  if (Index>=cdv->boardCount) return NULL;
  return cdv->boards[Index].info.szBoard;
  }

//***************************************************************************
