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

#ifndef _CGEBSDA_H_
#define _CGEBSDA_H_

//***************************************************************************

typedef struct {
  unsigned char day;          // day (BCD 01h-31h)
  unsigned char month;        // month(BCD 01h-12h)
  unsigned char year;         // year(BCD 05h-99h)
  } CGEB_DATE;

typedef struct {
  unsigned char cgTotalSize;         // total secure data area size in bytes
  unsigned char cgCheckSize;         // size of the checksummed area
  unsigned char cgChecksum;          // checksum correction byte
  unsigned char cgSerialNumber[6];   // board serial number (BCD)
  unsigned char cgPartNumber[16];    // board part number (ASCII)
  unsigned char cgEanCode[7];        // board EAN-13 code (BCD)
  unsigned char cgProductRevMaj;     // major product rev (ASCII)
  unsigned char cgProductRevMin;     // minor product rev (ASCII)
  CGEB_DATE cgMfgDate;               // date of manufacturing
  CGEB_DATE cgRepairDate;            // date of last repair
  unsigned char cgManufacturerId;    // manufacturer ID (hex)
  unsigned char cgProjectId[4];      // project ID (ASCII)
  unsigned char cgRepairCounter;     // repair counter (hex)
  unsigned char cgSecureDataRes[13]; // spare
  unsigned char cgBootCounter[3];    // board boot counter (hex)
  unsigned short cgRunningTime;      // board running time in hours (hex)
  } CGEB_SDA;

//***************************************************************************

#endif
