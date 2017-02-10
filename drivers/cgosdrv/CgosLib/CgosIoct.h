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

#ifndef _CGOSIOCT_H_
#define _CGOSIOCT_H_

//***************************************************************************

#define CGOS_DRIVER_MAJOR 1

//***************************************************************************

typedef struct {
  unsigned int fct;
  unsigned int handle;
  unsigned int type;
  unsigned int pars[4];
  } CGOSIOCTLIN;

typedef struct {
  unsigned int status;
  unsigned int rets[2];
  } CGOSIOCTLOUT;

//***************************************************************************

#define xCgosDrvGetVersion          0
#define xCgosBoardClose             1
#define xCgosBoardCount             2
#define xCgosBoardOpen              3
#define xCgosBoardOpenByNameA       4
#define xCgosBoardGetNameA          5
#define xCgosBoardGetInfoA          6
#define xCgosBoardGetBootCounter    7
#define xCgosBoardGetRunningTimeMeter 8
#define xCgosBoardGetOption         9
#define xCgosBoardSetOption        10
#define xCgosBoardGetBootErrorLog  11
#define xCgosVgaCount              12
#define xCgosVgaGetContrast        13
#define xCgosVgaSetContrast        14
#define xCgosVgaGetContrastEnable  15
#define xCgosVgaSetContrastEnable  16
#define xCgosVgaGetBacklight       17
#define xCgosVgaSetBacklight       18
#define xCgosVgaGetBacklightEnable 19
#define xCgosVgaSetBacklightEnable 20
#define xCgosVgaEndDarkBoot        21
#define xCgosVgaGetInfo            22
#define xCgosStorageAreaCount      23
#define xCgosStorageAreaType       24
#define xCgosStorageAreaSize       25
#define xCgosStorageAreaBlockSize  26
#define xCgosStorageAreaRead       27
#define xCgosStorageAreaWrite      28
#define xCgosStorageAreaErase      29
#define xCgosStorageAreaEraseStatus 30
#define xCgosI2CCount              31
#define xCgosI2CType               32
#define xCgosI2CIsAvailable        33
#define xCgosI2CRead               34
#define xCgosI2CWrite              35
#define xCgosI2CReadRegister       36
#define xCgosI2CWriteRegister      37
#define xCgosI2CWriteReadCombined  38
#define xCgosIOCount               39
#define xCgosIOIsAvailable         40
#define xCgosIORead                41
#define xCgosIOWrite               42
#define xCgosIOXorAndXor           43
#define xCgosIOGetDirection        44
#define xCgosIOSetDirection        45
#define xCgosIOGetDirectionCaps    46
#define xCgosIOGetNameA            47
#define xCgosWDogCount             48
#define xCgosWDogIsAvailable       49
#define xCgosWDogTrigger           50
#define xCgosWDogGetTriggerCount   51
#define xCgosWDogSetTriggerCount   52
#define xCgosWDogGetConfigStruct   53
#define xCgosWDogSetConfigStruct   54
#define xCgosWDogSetConfig         55
#define xCgosWDogDisable           56
#define xCgosWDogGetInfo           57
#define xCgosPerformanceGetCurrent 58
#define xCgosPerformanceSetCurrent 59
#define xCgosPerformanceGetPolicyCaps 60
#define xCgosPerformanceGetPolicy  61
#define xCgosPerformanceSetPolicy  62
#define xCgosTemperatureCount      63
#define xCgosTemperatureGetInfo    64
#define xCgosTemperatureGetCurrent 65
#define xCgosTemperatureSetLimits  66
#define xCgosFanCount              67
#define xCgosFanGetInfo            68
#define xCgosFanGetCurrent         69
#define xCgosFanSetLimits          70
#define xCgosVoltageCount          71
#define xCgosVoltageGetInfo        72
#define xCgosVoltageGetCurrent     73
#define xCgosVoltageSetLimits      74
#define xCgosCgeb                  75
#define xCgosCgebTransAddr         76
#define xCgosCgebDbgLevel          77
#define xCgosCgbcGetInfo           78
#define xCgosCgbcSetControl        79
#define xCgosCgbcReadWrite         80
#define xCgosCgbcHandleCommand     81
#define xCgosStorageAreaLock       82
#define xCgosStorageAreaUnlock     83
#define xCgosStorageAreaIsLocked   84
#define xCgosI2CGetMaxFrequency    85
#define xCgosI2CGetFrequency       86
#define xCgosI2CSetFrequency       87


//***************************************************************************

#ifndef CGOS_IOCTL
#ifdef CTL_CODE
#define CGOS_IOCTL (CTL_CODE(44444l,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS))
#else
#define CGOS_IOCTL 44444
#endif
#endif

//***************************************************************************

#endif

