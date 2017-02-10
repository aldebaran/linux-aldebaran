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

#ifndef _CGOS_H_
#define _CGOS_H_

#ifdef __cplusplus
extern "C" {
#endif

//***************************************************************************

#ifndef CGOSDLLAPI
#define CGOSDLLAPI
#endif

#ifndef CGOSAPI
#ifndef _WIN32
#define CGOSAPI
#elif defined(_MSC_VER) && (_MSC_VER >= 800)
#define CGOSAPI __stdcall
#else
#define CGOSAPI pascal
#endif
#endif

#define cgosret_bool CGOSDLLAPI unsigned int CGOSAPI
#define cgosret_ulong CGOSDLLAPI unsigned int CGOSAPI

//***************************************************************************

#define CgosLibVersionMajor 1
#define CgosLibVersionMinor 3
#define CgosLibVersion (((((unsigned int)CgosLibVersionMajor)<<8)+CgosLibVersionMinor)<<16)

//***************************************************************************

typedef struct {
  unsigned short wYear;
  unsigned short wMonth;
  unsigned short wDayOfWeek;
  unsigned short wDay;
  unsigned short wHour;
  unsigned short wMinute;
  unsigned short wSecond;
  unsigned short wMilliseconds;
  } CGOSTIME;

//***************************************************************************

#define CGOS_BOARD_MAX_LEN_ID_STRING 7
#define CGOS_BOARD_MAX_SIZE_ID_STRING 16
#define CGOS_BOARD_MAX_LEN_SERIAL_STRING 12
#define CGOS_BOARD_MAX_SIZE_SERIAL_STRING 16
#define CGOS_BOARD_MAX_LEN_PART_STRING 16
#define CGOS_BOARD_MAX_SIZE_PART_STRING 20
#define CGOS_BOARD_MAX_LEN_EAN_STRING 13
#define CGOS_BOARD_MAX_SIZE_EAN_STRING 20

typedef struct {
  unsigned int dwSize;
  unsigned int dwFlags;
  char szReserved[CGOS_BOARD_MAX_SIZE_ID_STRING];
  char szBoard[CGOS_BOARD_MAX_SIZE_ID_STRING];
  char szBoardSub[CGOS_BOARD_MAX_SIZE_ID_STRING];
  char szManufacturer[CGOS_BOARD_MAX_SIZE_ID_STRING];
  CGOSTIME stManufacturingDate;
  CGOSTIME stLastRepairDate;
  char szSerialNumber[CGOS_BOARD_MAX_SIZE_SERIAL_STRING];
  unsigned short wProductRevision;
  unsigned short wSystemBiosRevision;
  unsigned short wBiosInterfaceRevision;
  unsigned short wBiosInterfaceBuildRevision;
  unsigned int dwClasses;
  unsigned int dwPrimaryClass;
  unsigned int dwRepairCounter;
  char szPartNumber[CGOS_BOARD_MAX_SIZE_PART_STRING];
  char szEAN[CGOS_BOARD_MAX_SIZE_EAN_STRING];
  unsigned int dwManufacturer;
  } CGOSBOARDINFOA;

typedef struct {
  unsigned int dwSize;
  unsigned int dwFlags;
  wchar_t szReserved[CGOS_BOARD_MAX_SIZE_ID_STRING];
  wchar_t szBoard[CGOS_BOARD_MAX_SIZE_ID_STRING];
  wchar_t szBoardSub[CGOS_BOARD_MAX_SIZE_ID_STRING];
  wchar_t szManufacturer[CGOS_BOARD_MAX_SIZE_ID_STRING];
  CGOSTIME stManufacturingDate;
  CGOSTIME stLastRepairDate;
  wchar_t szSerialNumber[CGOS_BOARD_MAX_SIZE_SERIAL_STRING];
  unsigned short wProductRevision;
  unsigned short wSystemBiosRevision;
  unsigned short wBiosInterfaceRevision;
  unsigned short wBiosInterfaceBuildRevision;
  unsigned int dwClasses;
  unsigned int dwPrimaryClass;
  unsigned int dwRepairCounter;
  wchar_t szPartNumber[CGOS_BOARD_MAX_SIZE_PART_STRING];
  wchar_t szEAN[CGOS_BOARD_MAX_SIZE_EAN_STRING];
  unsigned int dwManufacturer;
  } CGOSBOARDINFOW;

#ifdef UNICODE
#define CGOSBOARDINFO CGOSBOARDINFOW
#define CGOSTCHAR wchar_t
#else
#define CGOSBOARDINFO CGOSBOARDINFOA
#define CGOSTCHAR char
#endif

//***************************************************************************

//
// Board handle
//

typedef unsigned int HCGOS;

//
// Board Classes
//

#define CGOS_BOARD_CLASS_CPU 0x00000001
#define CGOS_BOARD_CLASS_VGA 0x00000002
#define CGOS_BOARD_CLASS_IO  0x00000004

//
// Board Open/Count Flags
//

#define CGOS_BOARD_OPEN_FLAGS_DEFAULT 0
#define CGOS_BOARD_OPEN_FLAGS_PRIMARYONLY 1

//
// Max values for LCD settings
//

#define CGOS_VGA_CONTRAST_MAX 100
#define CGOS_VGA_BACKLIGHT_MAX 100

// CgosVgaGetInfo

#define CGOS_VGA_TYPE_UNKNOWN  0x00000000
#define CGOS_VGA_TYPE_CRT      0x00010000
#define CGOS_VGA_TYPE_LCD      0x00020000
#define CGOS_VGA_TYPE_LCD_DVO  0x00020001
#define CGOS_VGA_TYPE_LCD_LVDS 0x00020002
#define CGOS_VGA_TYPE_TV       0x00030000

typedef struct {
  unsigned int dwSize;
  unsigned int dwType;
  unsigned int dwFlags;
  unsigned int dwNativeWidth;
  unsigned int dwNativeHeight;
  unsigned int dwRequestedWidth;
  unsigned int dwRequestedHeight;
  unsigned int dwRequestedBpp;
  unsigned int dwMaxBacklight;
  unsigned int dwMaxContrast;
  } CGOSVGAINFO;

//
// Type identifiers for storage areas
//

#define CGOS_STORAGE_AREA_UNKNOWN   0
#define CGOS_STORAGE_AREA_EEPROM    0x00010000
#define CGOS_STORAGE_AREA_FLASH     0x00020000
#define CGOS_STORAGE_AREA_CMOS      0x00030000
#define CGOS_STORAGE_AREA_RAM       0x00040000

//
// I2C bus types returned by CgosI2CType()
//

#define CGOS_I2C_TYPE_UNKNOWN 0           // I2C bus for unknown or special purposes
#define CGOS_I2C_TYPE_PRIMARY 0x00010000  // primary I2C bus
#define CGOS_I2C_TYPE_SMB     0x00020000  // system management bus
#define CGOS_I2C_TYPE_DDC     0x00030000  // I2C bus of the DDC interface

//***************************************************************************

//
// Watchdog
//

#define CGOS_WDOG_MODE_REBOOT_PC    0
#define CGOS_WDOG_MODE_RESTART_OS   1
#define CGOS_WDOG_MODE_STAGED    0x80

#define CGOS_WDOG_OPMODE_DISABLED      0
#define CGOS_WDOG_OPMODE_ONETIME_TRIG  1
#define CGOS_WDOG_OPMODE_SINGLE_EVENT  2
#define CGOS_WDOG_OPMODE_EVENT_REPEAT  3

#define CGOS_WDOG_EVENT_INT 0 // NMI/IRQ
#define CGOS_WDOG_EVENT_SCI 1 // SMI/SCI
#define CGOS_WDOG_EVENT_RST 2 // system reset
#define CGOS_WDOG_EVENT_BTN 3 // power button

#define CGOS_WDOG_EVENT_MAX_STAGES 3

typedef struct {
  unsigned int dwTimeout;
  unsigned int dwEvent;
  } CGOSWDSTAGE;

typedef struct {
  unsigned int dwSize;
  unsigned int dwTimeout; // not used in staged mode
  unsigned int dwDelay;
  unsigned int dwMode;
  // optional parameters for staged watchdog
  unsigned int dwOpMode;
  unsigned int dwStageCount;
  CGOSWDSTAGE stStages[CGOS_WDOG_EVENT_MAX_STAGES];
  } CGOSWDCONFIG;

// Watch dog info

#define CGOS_WDOG_TYPE_UNKNOWN  0
#define CGOS_WDOG_TYPE_BC       0x00020000
#define CGOS_WDOG_TYPE_CHIPSET  0x00030000

typedef struct {
  unsigned int dwSize;
  unsigned int dwFlags;
  unsigned int dwMinTimeout;
  unsigned int dwMaxTimeout;
  unsigned int dwMinDelay;
  unsigned int dwMaxDelay;
  unsigned int dwOpModes;         // supported operation mode mask (1<<opmode)
  unsigned int dwMaxStageCount;
  unsigned int dwEvents;          // supported event mask (1<<event)
  unsigned int dwType;
  } CGOSWDINFO;


//***************************************************************************

//
// Temperature, fan, and voltage structures
//

// Temperature in units of 1/1000th degrees celcius

typedef struct {
  unsigned int dwSize;
  unsigned int dwType;
  unsigned int dwFlags;
  unsigned int dwAlarm;
  unsigned int dwRes;
  unsigned int dwMin;
  unsigned int dwMax;
  unsigned int dwAlarmHi;
  unsigned int dwHystHi;
  unsigned int dwAlarmLo;
  unsigned int dwHystLo;
  } CGOSTEMPERATUREINFO;

// Fan speed values in RPM (revolutions per minute)

typedef struct {
  unsigned int dwSize;
  unsigned int dwType;
  unsigned int dwFlags;
  unsigned int dwAlarm;
  unsigned int dwSpeedNom;
  unsigned int dwMin;
  unsigned int dwMax;
  unsigned int dwAlarmHi;
  unsigned int dwHystHi;
  unsigned int dwAlarmLo;
  unsigned int dwHystLo;
  unsigned int dwOutMin;
  unsigned int dwOutMax;
  } CGOSFANINFO;

// Voltage in units of 1/1000th volt

typedef struct {
  unsigned int dwSize;
  unsigned int dwType;
  unsigned int dwNom;
  unsigned int dwFlags;
  unsigned int dwAlarm;
  unsigned int dwRes;
  unsigned int dwMin;
  unsigned int dwMax;
  unsigned int dwAlarmHi;
  unsigned int dwHystHi;
  unsigned int dwAlarmLo;
  unsigned int dwHystLo;
  } CGOSVOLTAGEINFO;

// Types

#define CGOS_TEMP_CPU           0x00010000
#define CGOS_TEMP_BOX           0x00020000
#define CGOS_TEMP_ENV           0x00030000
#define CGOS_TEMP_BOARD         0x00040000
#define CGOS_TEMP_BACKPLANE     0x00050000
#define CGOS_TEMP_CHIPSETS      0x00060000
#define CGOS_TEMP_VIDEO         0x00070000
#define CGOS_TEMP_OTHER         0x00080000
#define CGOS_TEMP_TOPDIMM_ENV   0x00090000      // Top DIMM module environment temperature
#define CGOS_TEMP_BOTDIMM_ENV   0x000A0000      // Bottom DIMM module environment temperature

#define CGOS_FAN_CPU            0x00010000
#define CGOS_FAN_BOX            0x00020000
#define CGOS_FAN_ENV            0x00030000
#define CGOS_FAN_CHIPSET        0x00040000
#define CGOS_FAN_VIDEO          0x00050000
#define CGOS_FAN_OTHER          0x00060000

#define CGOS_VOLTAGE_CPU        0x00010000
#define CGOS_VOLTAGE_DC         0x00020000
#define CGOS_VOLTAGE_DC_STANDBY 0x00030000
#define CGOS_VOLTAGE_BAT_CMOS   0x00040000
#define CGOS_VOLTAGE_BAT_POWER  0x00050000
#define CGOS_VOLTAGE_AC         0x00060000
#define CGOS_VOLTAGE_OTHER      0x00070000
#define CGOS_VOLTAGE_5V_S0      0x00080000
#define CGOS_VOLTAGE_5V_S5      0x00090000
#define CGOS_VOLTAGE_33V_S0     0x000A0000
#define CGOS_VOLTAGE_33V_S5     0x000B0000
#define CGOS_VOLTAGE_VCOREA     0x000C0000
#define CGOS_VOLTAGE_VCOREB     0x000D0000
#define CGOS_VOLTAGE_12V_S0     0x000E0000

// Due to missing CGOS current sensor definitions 
// the values of the existing current sensors are 
// returned in CGOSVOLTAGEINFO as voltage drop of the 
// respective input current on a 1 Ohm resistor.
// This allows direct interpretation of this voltage 
// as input current value in 1/1000th amperes.
#define CGOS_VCURRENT_DC       	0x80020000
#define CGOS_VCURRENT_5V_S0     0x80080000
#define CGOS_VCURRENT_12V_S0    0x800E0000

// Temperature, fan, voltage status flags

#define CGOS_SENSOR_ACTIVE 0x01
#define CGOS_SENSOR_ALARM 0x02
#define CGOS_SENSOR_BROKEN 0x04
#define CGOS_SENSOR_SHORTCIRCUIT 0x08

//***************************************************************************

//
// Performance
//

#define CGOS_CPU_PERF_THROTTLING 1
#define CGOS_CPU_PERF_FREQUENCY 2

//***************************************************************************

#ifndef NOCGOSAPI

// Library

cgosret_ulong CgosLibGetVersion(void);
cgosret_bool CgosLibInitialize(void);
cgosret_bool CgosLibUninitialize(void);
cgosret_bool CgosLibIsAvailable(void);
cgosret_bool CgosLibInstall(unsigned int install);
cgosret_ulong CgosLibGetDrvVersion(void);
cgosret_ulong CgosLibGetLastError(void); // 1.2
cgosret_bool CgosLibSetLastErrorAddress(unsigned int *pErrNo); // 1.2

// Generic board

cgosret_bool CgosBoardClose(HCGOS hCgos);

cgosret_ulong CgosBoardCount(unsigned int dwClass, unsigned int dwFlags);
cgosret_bool CgosBoardOpen(unsigned int dwClass, unsigned int dwNum, unsigned int dwFlags, HCGOS *phCgos);
cgosret_bool CgosBoardOpenByNameA(const char *pszName, HCGOS *phCgos);
cgosret_bool CgosBoardGetNameA(HCGOS hCgos, char *pszName, unsigned int dwSize);
cgosret_bool CgosBoardGetInfoA(HCGOS hCgos, CGOSBOARDINFOA *pBoardInfo);

cgosret_bool CgosBoardGetBootCounter(HCGOS hCgos, unsigned int *pdwCount);
cgosret_bool CgosBoardGetRunningTimeMeter(HCGOS hCgos, unsigned int *pdwCount);

// VGA (LCD)

cgosret_ulong CgosVgaCount(HCGOS hCgos);
cgosret_bool CgosVgaGetContrast(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosVgaSetContrast(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);
cgosret_bool CgosVgaGetContrastEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosVgaSetContrastEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);
cgosret_bool CgosVgaGetBacklight(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosVgaSetBacklight(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);
cgosret_bool CgosVgaGetBacklightEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosVgaSetBacklightEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);
cgosret_bool CgosVgaGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSVGAINFO *pInfo);

// Storage Areas

cgosret_ulong CgosStorageAreaCount(HCGOS hCgos, unsigned int dwUnit);
cgosret_ulong CgosStorageAreaType(HCGOS hCgos, unsigned int dwUnit);
cgosret_ulong CgosStorageAreaSize(HCGOS hCgos, unsigned int dwUnit);
cgosret_ulong CgosStorageAreaBlockSize(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosStorageAreaRead(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned char *pBytes, unsigned int dwLen);
cgosret_bool CgosStorageAreaWrite(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned char *pBytes, unsigned int dwLen);
cgosret_bool CgosStorageAreaErase(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned int dwLen);
cgosret_bool CgosStorageAreaEraseStatus(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned int dwLen, unsigned int *lpStatus);
cgosret_bool CgosStorageAreaLock(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags, unsigned char *pBytes, unsigned int dwLen); // 1.2
cgosret_bool CgosStorageAreaUnlock(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags, unsigned char *pBytes, unsigned int dwLen); // 1.2
cgosret_bool CgosStorageAreaIsLocked(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags); // 1.2

// I2C Bus

cgosret_ulong CgosI2CCount(HCGOS hCgos);
cgosret_ulong CgosI2CType(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosI2CIsAvailable(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosI2CRead(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytes, unsigned int dwLen);
cgosret_bool CgosI2CWrite(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytes, unsigned int dwLen);

cgosret_bool CgosI2CReadRegister(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned short wReg, unsigned char *pDataByte);
cgosret_bool CgosI2CWriteRegister(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned short wReg, unsigned char bData);

cgosret_bool CgosI2CWriteReadCombined(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytesWrite,
  unsigned int dwLenWrite, unsigned char *pBytesRead, unsigned int dwLenRead);

cgosret_bool CgosI2CGetMaxFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting); // 1.3
cgosret_bool CgosI2CGetFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting); // 1.3
cgosret_bool CgosI2CSetFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting); // 1.3

// General purpose IO

cgosret_ulong CgosIOCount(HCGOS hCgos);
cgosret_bool CgosIOIsAvailable(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosIORead(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwData);
cgosret_bool CgosIOWrite(HCGOS hCgos, unsigned int dwUnit, unsigned int dwData);
cgosret_bool CgosIOXorAndXor(HCGOS hCgos, unsigned int dwUnit, unsigned int dwXorMask1, unsigned int dwAndMask, unsigned int dwXorMask2);
cgosret_bool CgosIOGetDirection(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwData);
cgosret_bool CgosIOSetDirection(HCGOS hCgos, unsigned int dwUnit, unsigned int dwData);
cgosret_bool CgosIOGetDirectionCaps(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwInputs, unsigned int *pdwOutputs);
cgosret_bool CgosIOGetNameA(HCGOS hCgos, unsigned int dwUnit, char *pszName, unsigned int dwSize);

// Watchdog

cgosret_ulong CgosWDogCount(HCGOS hCgos);
cgosret_bool CgosWDogIsAvailable(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosWDogTrigger(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosWDogGetConfigStruct(HCGOS hCgos, unsigned int dwUnit, CGOSWDCONFIG *pConfig);
cgosret_bool CgosWDogSetConfigStruct(HCGOS hCgos, unsigned int dwUnit, CGOSWDCONFIG *pConfig);
cgosret_bool CgosWDogSetConfig(HCGOS hCgos, unsigned int dwUnit, unsigned int timeout, unsigned int delay, unsigned int mode);
cgosret_bool CgosWDogDisable(HCGOS hCgos, unsigned int dwUnit);
cgosret_bool CgosWDogGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSWDINFO *pInfo);

// CPU Performance

cgosret_bool CgosPerformanceGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosPerformanceSetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);
cgosret_bool CgosPerformanceGetPolicyCaps(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosPerformanceGetPolicy(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
cgosret_bool CgosPerformanceSetPolicy(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);

// Temperature

cgosret_ulong CgosTemperatureCount(HCGOS hCgos);
cgosret_bool CgosTemperatureGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSTEMPERATUREINFO *pInfo);
cgosret_bool CgosTemperatureGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus);
cgosret_bool CgosTemperatureSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSTEMPERATUREINFO *pInfo);

// Fan

cgosret_ulong CgosFanCount(HCGOS hCgos);
cgosret_bool CgosFanGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSFANINFO *pInfo);
cgosret_bool CgosFanGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus);
cgosret_bool CgosFanSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSFANINFO *pInfo);

// Voltage

cgosret_ulong CgosVoltageCount(HCGOS hCgos);
cgosret_bool CgosVoltageGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSVOLTAGEINFO *pInfo);
cgosret_bool CgosVoltageGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus);
cgosret_bool CgosVoltageSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSVOLTAGEINFO *pInfo);

//***************************************************************************

// Unicode

cgosret_bool CgosBoardOpenByNameW(const wchar_t *pszName, HCGOS *phCgos);
cgosret_bool CgosBoardGetNameW(HCGOS hCgos, wchar_t *pszName, unsigned int dwSize);
cgosret_bool CgosBoardGetInfoW(HCGOS hCgos, CGOSBOARDINFOW *pBoardInfo);
cgosret_bool CgosIOGetNameW(HCGOS hCgos, unsigned int dwUnit, wchar_t *pszName, unsigned int dwSize);

#ifdef UNICODE
#define CgosBoardOpenByName CgosBoardOpenByNameW
#define CgosBoardGetName CgosBoardGetNameW
#define CgosBoardGetInfo CgosBoardGetInfoW
#define CgosIOGetName CgosIOGetNameW
#else
#define CgosBoardOpenByName CgosBoardOpenByNameA
#define CgosBoardGetName CgosBoardGetNameA
#define CgosBoardGetInfo CgosBoardGetInfoA
#define CgosIOGetName CgosIOGetNameA
#endif

#endif

//***************************************************************************

#ifdef __cplusplus
}
#endif

#endif // _CGOS_H_

