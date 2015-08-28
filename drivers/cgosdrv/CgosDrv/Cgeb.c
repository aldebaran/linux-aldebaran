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

#ifdef DBPF

char *CgebNames[]={
  // wait until function numbers are stable
  };

#define dbpfCgebNames(names,fct) \
  { if (fct<sizeof(names)/sizeof(*names)) \
  dbpf((TT("CGOS: xCgeb%s\n"),names[fct])); }

#define dbpfCgebName(fct) \
  { dbpfCgebNames(CgebNames,fct) }

#else

#define dbpfCgebName(fct)

#endif

//***************************************************************************

#if defined(CGEB_LINKED_IN) || defined (AMD64)

unsigned short GetDS(void)
  {
  return 0;
  }

#elif defined _MSC_VER // MS VC

#pragma warning(disable: 4035)

unsigned short GetDS(void)
  {
  _asm mov ax,ds
  }

#pragma warning(default: 4035)

#elif defined __WATCOMC__

unsigned short GetDS(void);
#pragma aux GetDS="mov ax,ds" value [ax];

#elif defined linux

// this simply uses inline GCC assembly syntax. Ensures that the code
// is correctly placed into .text segment. Avoids NX faults.
static __inline__ unsigned short GetDS(void) 
  {
	  unsigned short dseg;
	  asm("mov %%ds, %0;" 
		:"=a"(dseg)
		: /* No inputs */
		);
	  return dseg;
  }

#else // GNU C

// this would work with MSVC as well but XPSP2++ execute protection could cause problems

unsigned short GetDS(void)
  {
  static unsigned char GetDSRaw[]= { // cs would be 0xC8
    0x8C, 0xD8,         // mov ax,ds
    0xC3                // ret
    };
  return ((unsigned short (*)(void))GetDSRaw)();
  }

#endif // CGEB_LINKED_IN

#if (!defined(_MSC_VER) && !defined(__cdecl))
//MODGWE #define __cdecl
#define __cdecl __attribute__((regparm(0)))		//MODGWE
#endif

void CgebCall(CGEBFPS *fps, void *addr)
  {
#if !defined(AMD64) || defined(CGEBEMU)
  // addr points to a bimodal C style function that expects a far pointer to an fps.
  // if cs is 0 then it does a near return, otherwise a far return.
  // if we ever need a far return then we must not pass cs at all.
  // parameters are removed by the caller
  ((void (__cdecl *)(unsigned short cs, CGEBFPS *fps, unsigned short ds))addr)(0,fps,fps->data.seg);
#else
  // CGEBQ expects FPS in edx (ecx is reserved and must be 0)
  // x64 (MS) calling convention is rcx, rdx, r8, r9
  // ABI (linux) calling convention rdi, rsi, rdx, rcx
  // parameters are removed by the caller
  ((void (*)(void *, CGEBFPS *fps, CGEBFPS *fps_abi, void *))addr)(NULL,fps,fps,NULL);
#endif
  }

//***************************************************************************

unsigned long CgebInvokeStep(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps)
  {
  dbpf((TT("CGOS: CGEB: ->  size %02X, fct %02X, data %04X:%08X, status %08X\n"),
    fps->size,fps->fct,fps->data.seg,fps->data.off,fps->status));
  CgebCall(fps,cgeb->entry);
  dbpf((TT("CGOS:  <- CGEB: size %02X, fct %02X, data %04X:%08X, status %08X\n"),
    fps->size,fps->fct,fps->data.seg,fps->data.off,fps->status));
  return fps->status;
  }

unsigned long CgebInvokeFirstStep(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned long size, unsigned long fct)
  {
  fps->size=(size)?size:sizeof(CGEBFPS);
  fps->fct=fct;
  fps->data.off=cgeb->data;
  fps->data.seg=cgeb->ds;
  fps->data.pad=0;
/*
  fps->ptrs[0].seg=cgeb->ds;
  fps->ptrs[0].pad=0;
  fps->ptrs[1].seg=cgeb->ds;
  fps->ptrs[1].pad=0;
*/
  fps->status=CGEB_SUCCESS;
  fps->cont=fps->subfct=fps->subfps=0;
  if (!cgeb->entry) return CGEB_ERROR;
  dbpfCgebName(fct);
  return CgebInvokeStep(cgeb,fps);
  }

unsigned int CgebInvoke(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned long size, unsigned long fct)
  {
  CgebInvokeFirstStep(cgeb,fps,size,fct);
  for (;;) {
    switch (fps->status) {
      case CGEB_SUCCESS: return TRUE;
      case CGEB_NEXT: break; // simply call again

      case CGEB_NOIRQS:
        OsaSleepms(cgeb,0); // call again with a full time slice
        break;

      case CGEB_DELAY:
        if (fps->rets[0]<1000) // less then 1 ms
          OsaWaitus(cgeb,fps->rets[0]);
        else
          OsaSleepms(cgeb,(fps->rets[0]+999)/1000);
        break;

      case CGEB_DBG_STR:
//        if (fps->rets[0]) dbpf((TT("CGEB: %s\n"),(unsigned char *)fps->rets[0]));
        if (fps->optr) dbpf((TT("CGEB: %s\n"),(unsigned char *)fps->optr));
        break;

      case CGEB_DBG_HEX: dbpf((TT("CGEB: 0x%08X\n"),fps->rets[0])); break;
      case CGEB_DBG_DEC: dbpf((TT("CGEB: %d\n"),fps->rets[0])); break;

      default:
        if ((long)fps->status>0) // unknown continuation code
          fps->status=CGEB_ERROR;
        return FALSE;
      }
    CgebInvokeStep(cgeb,fps);
    }
  }

unsigned int CgebInvokePlain(CGOS_DRV_CGEB *cgeb, unsigned long fct, unsigned long *pRes)
  {
  CGEBFPS fps;
  unsigned int ret;
  OsaMemSet(&fps,0,sizeof(fps));
  ret=CgebInvoke(cgeb,&fps,sizeof(fps),fct);
  if (pRes) *pRes=fps.rets[0];
  return ret;
  }

unsigned int CgebInvokeRetUnit(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct, unsigned long unit, unsigned long *pRes)
  {
  CGEBFPS fps;
  unsigned int ret;
  OsaMemSet(&fps,0,sizeof(fps));
  fps.unit=unit;
  ret=CgebInvoke(cgeb,&fps,sizeof(fps),fct);
  if (pRes) {
    *pRes=fps.status;
    if (flags&1) *pRes=fps.rets[0];
    if (flags&2) *pRes=fps.status;
    if (flags&4) *(void **)pRes=fps.optr;
    }
  return ret;
  }

unsigned int CgebInvokeRet(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct, unsigned long *pRes)
  {
  return CgebInvokeRetUnit(cgeb,flags,fct,0,pRes);
  }

unsigned int CgebInvokeVoid(CGOS_DRV_CGEB *cgeb, CGEBFPS *fps, unsigned long size)
  {
  if (size<sizeof(CGEBFPS)) return FALSE;
  return CgebInvoke(cgeb,fps,size,fps->fct);
  }


unsigned long CgebInvokePar(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
    unsigned long unit, unsigned long par0, unsigned long par1,
    unsigned long par2, unsigned long par3, unsigned long *pret0, unsigned long *pret1)
  {
  CGEBFPS fps;
  fps.fct=fct;
  fps.unit=unit;
  fps.pars[0]=par0;
  fps.pars[1]=par1;
  fps.pars[2]=par2;
  fps.pars[3]=par3;
  fps.iptr=0;
  fps.optr=0;
  if (!CgebInvoke(cgeb,&fps,sizeof(fps),fct)) return FALSE;
  if (pret0) *pret0=fps.rets[0];
  if (pret1) *pret1=fps.rets[1];
  if (flags&1) return fps.rets[0];
  if (flags&2) return fps.status;
//  if (flags&4) return (unsigned long)fps.optr; // unsupported
  return TRUE;
  }

unsigned long CgebInvokeIoctl(CGOS_DRV_CGEB *cgeb, unsigned long flags, unsigned long fct,
  CGOS_DRV_VARS *cdv)
  {
  CGEBFPS fps;
  CGOSIOCTLIN *cin=cdv->cin;
  CGOSIOCTLOUT *cout=cdv->cout;
  unsigned int fixlen=0;
  fps.fct=fct;
  fps.unit=cdv->unit; //cin->type;
  fps.pars[0]=cin->pars[0];
  fps.pars[1]=cin->pars[1];
  fps.pars[2]=cin->pars[2];
  fps.pars[3]=cin->pars[3];
  fps.iptr=0;
  fps.optr=0;
  if (cdv->lout) fps.optr=cout+1;
  if (cdv->lin) fps.iptr=cin+1;
  if (!CgebInvoke(cgeb,&fps,sizeof(fps),fct)) return FALSE;
  if (flags&16 && fps.optr) {
    cdv->retcnt=*(unsigned long *)fps.optr; // retcnt from returned buffer size
    if (cdv->retcnt>cdv->lout) {
      cdv->retcnt=cdv->lout;
      fixlen=1;
      }
    }
  if (cdv->retcnt && fps.optr && fps.optr!=cout+1)
    OsaMemCpy(cout+1,fps.optr,cdv->retcnt); // CGEB returned a different fps.optr so we must copy
  if (fixlen) *(unsigned long *)(cout+1)=cdv->lout;
  // on these CGEB always uses the caller supplied buffer, so there's no need to copy
  if (flags&32) cdv->retcnt=fps.rets[0]; // number of bytes read
  if (flags&64) cdv->retcnt=fps.pars[0]; // number of bytes read from input pars0
  if (flags&128) cdv->retcnt=fps.pars[1]; // number of bytes read from input pars1
  cout->rets[0]=fps.rets[0];
  cout->rets[1]=fps.rets[1];
  cdv->status=fps.status;
  if (flags&1) return fps.rets[0];
  if (flags&2) return fps.status;
  return TRUE;
  }

//***************************************************************************
//***************************************************************************

unsigned int CgebInvokePars(CGOS_DRV_CGEB *cgeb, unsigned long fct, unsigned long num,
    unsigned long *pars, unsigned int back, unsigned long *pRes)
  {
  CGEBFPS_PARS fps;
  unsigned int ret;
  unsigned long i;
  if (num>CGEB_PARS_MAX) return FALSE;
  for (i=0; i<num; i++) fps.pars[i]=pars[i];
  ret=CgebInvoke(cgeb,&fps.fps,CGEB_PARS_SIZE(num),fct);
  if (back && ret)
    for (i=0; i<num; i++) pars[i]=fps.pars[i];
  *pRes=fps.fps.rets[0];
  return ret;
  }

//***************************************************************************
//***************************************************************************

void OsaWaitus(void *ctx, unsigned long us)
  {
  CGEBFPS fps;
  OsaMemSet(&fps,0,sizeof(fps));
  if (!ctx || CgebInvokeFirstStep((CGOS_DRV_CGEB *)ctx,&fps,sizeof(fps),xCgebDelayUs)) {
    unsigned long i;
    // do the best we can
    while (us--)
      for (i=10000; i; i--);
    }
  }

//***************************************************************************
//***************************************************************************

unsigned char *CgebScanMem(unsigned char *p, unsigned char *pend, char *magic)
  {
  unsigned long magic0=((unsigned long *)magic)[0];
  unsigned long magic1=((unsigned long *)magic)[1];
  for (; p<pend; p+=16)
    if (*(unsigned long *)p==magic0 && ((unsigned long *)p)[1]==magic1)
      return p;
  return NULL;
  }

unsigned int SdaReadToInfo(CGOS_DRV_BOARD *brd);

unsigned int CgebOpenRaw(CGOS_DRV_BOARD *brd, unsigned char *pcur, unsigned char *pend, unsigned char **ppnext)
  {
  unsigned long dw, i;
  CGOS_DRV_CGEB *cgeb=&brd->cgeb;
  CGOSBOARDINFOA *pb=&brd->info;
  CGEB_BOARDINFO *pbi;
  CGEB_LO_DESC *loDesc;
  CGEB_HI_DESC *hiDesc=NULL;
  unsigned long hiStart=0xFFF00000;
  CGEB_STORAGEAREA_INFO *psto;

  OsaMemSet(cgeb,0,sizeof(CGOS_DRV_CGEB));

  if (!pend)
    cgeb->entry=pcur; // if pend is NULL then pcur is the entry function
  else {
    dbpf((TT("CGOS: Looking for CGEB lo desc between virt 0x%X and 0x%X\n"),pcur,pend));

    // look for the next CGEB descriptor
    *ppnext=NULL;
    pcur=CgebScanMem(pcur,pend,CGEB_LD_MAGIC);

    if (!pcur) return FALSE; // nothing found
    *ppnext=pcur+16; // next paragraph

    dbpf((TT("CGOS: Found CGEB_LD_MAGIC\n")));

    loDesc=(CGEB_LO_DESC *)pcur;
    if (loDesc->size<sizeof(CGEB_LO_DESC)-sizeof(long)) return FALSE;
    if (loDesc->size>=sizeof(CGEB_LO_DESC) && loDesc->hiDescPhysAddr)
      hiStart=loDesc->hiDescPhysAddr;

    cgeb->hiDescLen=(unsigned long)-(long)hiStart;
    dbpf((TT("CGOS: Looking for CGEB hi desc between phys 0x%X and 0x%X\n"),hiStart,-1));
    cgeb->hiDescStart=(unsigned char *)OsaMapAddress(hiStart,cgeb->hiDescLen);
    if (!cgeb->hiDescStart) return FALSE;
    dbpf((TT("CGOS: Looking for CGEB hi desc between virt 0x%X and 0x%X\n"),cgeb->hiDescStart,cgeb->hiDescStart+cgeb->hiDescLen-1));
    hiDesc=(CGEB_HI_DESC *)CgebScanMem(cgeb->hiDescStart,cgeb->hiDescStart+cgeb->hiDescLen-1,CGEB_HD_MAGIC);
    if (!hiDesc) return FALSE;

    dbpf((TT("CGOS: Found CGEB_HD_MAGIC\n")));

    if (hiDesc->size<sizeof(CGEB_HI_DESC)) return FALSE;

    dbpf((TT("CGOS: dataSize %u, codeSize %u, entryRel %u\n"),hiDesc->dataSize,hiDesc->codeSize,hiDesc->entryRel));

    cgeb->code=OsaMemAlloc(hiDesc->codeSize);
    if (!cgeb->code) return FALSE;

    // copy the code
    OsaMemCpy(cgeb->code,hiDesc,hiDesc->codeSize);

    // free the hi descriptor mapping and switch the pointer to our copied code
    OsaUnMapAddress(cgeb->hiDescStart,cgeb->hiDescLen);
    cgeb->hiDescStart=NULL;
    hiDesc=(CGEB_HI_DESC *)cgeb->code;

    cgeb->entry=(void *)((unsigned char *)cgeb->code+hiDesc->entryRel);
    }
  dbpf((TT("CGOS: entry point at 0x%X\n"),cgeb->entry));
  cgeb->ds=GetDS();

  // check the revision
  if (!CgebInvokePlain(cgeb,xCgebGetCgebVersion,&dw)) return FALSE;
  if (CGEB_GET_VERSION_MAJOR(dw)!=CGEB_VERSION_MAJOR) return FALSE;
  pb->dwSize=sizeof(*pb);
  pb->wBiosInterfaceRevision=(unsigned short)(dw>>16);
  pb->wBiosInterfaceBuildRevision=(unsigned short)dw;

  // allocate the data area
  if (hiDesc && hiDesc->dataSize) {
    cgeb->data=OsaMemAlloc(hiDesc->dataSize);
    if (!cgeb->data) return FALSE;
    }
  else if (CgebInvokePlain(cgeb,xCgebGetDataSize,&dw) && dw) {
    cgeb->data=OsaMemAlloc(dw);
    if (!cgeb->data) return FALSE;
    }

  // init the data
  if (!CgebInvokePlain(cgeb,xCgebOpen,NULL)) return FALSE;

  if (CgebInvokeRet(cgeb,4,xCgebMapGetMem,(void *)&cgeb->mapMem) && cgeb->mapMem) {
    CGEB_MAP_MEM_LIST *pmm;
    CGEB_MAP_MEM * pmme;
    unsigned long i;
    pmm=(CGEB_MAP_MEM_LIST *)cgeb->mapMem;
    pmme=pmm->entries;
    dbpf((TT("CGOS: Memory Map with %u entries\n"),pmm->count));
    for (i=pmm->count; i; i--, pmme++) {
      if (pmme->phys && pmme->size) {
        pmme->virt.off=OsaMapAddress((unsigned long)pmme->phys,pmme->size); // !!! upper 32 bits are lost !!!
        if (!pmme->virt.off) return FALSE;
        }
      else pmme->virt.off=0;
      pmme->virt.seg=(pmme->virt.off)?cgeb->ds:0;
      dbpf((TT("CGOS:   Map phys %08X, size %08X, virt %04X:%08X\n"),
        pmme->phys,pmme->size,pmme->virt.seg,pmme->virt.off));
      }
    CgebInvokePlain(cgeb,xCgebMapChanged,NULL);
    }

  // get the board info
  if (!CgebInvokeRet(cgeb,4,xCgebBoardGetInfo,(void *)&pbi)) return FALSE;

  dbpf((TT("CGOS: Board name: %c%c%c%c\n"),pbi->szBoard[0],pbi->szBoard[1],pbi->szBoard[2],pbi->szBoard[3]));

  // store the board info
  OsaMemCpy(pb->szBoard,pbi->szBoard,sizeof(pb->szBoard));
  pb->dwPrimaryClass=pbi->dwPrimaryClass;
  pb->dwClasses=pbi->dwClasses|pbi->dwPrimaryClass;
  if (pbi->dwSize>(unsigned long)((char *)pbi->szVendor-(char *)pbi) && *pbi->szVendor)
    OsaMemCpy(pb->szManufacturer,pbi->szVendor,sizeof(pbi->szVendor));
  else
    OsaMemCpy(pb->szManufacturer,"congatec",9);

  // storage areas
  CgebInvokePlain(cgeb,xCgebStorageAreaCount,&brd->stoCount);
  if (brd->stoCount>CGOS_DRV_STO_MAX) brd->stoCount=CGOS_DRV_STO_MAX;
  for (i=0; i<brd->stoCount; i++)
    if (CgebInvokeRetUnit(cgeb,4,xCgebStorageAreaGetInfo,i,(void *)&psto) && psto)
      brd->sto[i].info=*psto;

  CgebInvokePlain(cgeb,xCgebI2CCount,&brd->i2cCount);
//  if (brd->i2cCount>CGOS_DRV_I2C_MAX) brd->i2cCount=CGOS_DRV_I2C_MAX;

  CgebInvokePlain(cgeb,xCgebWDogCount,&brd->wdogCount);
  if (brd->wdogCount>CGOS_DRV_WDOG_MAX) brd->wdogCount=CGOS_DRV_WDOG_MAX;

  // complete the board info, it needs the storage areas
  SdaReadToInfo(brd);
  if (CgebInvokePlain(cgeb,xCgebGetSysBiosVersion,&dw))
    pb->wSystemBiosRevision=(unsigned short)(dw>>16);

  dbpf((TT("CGOS: CgebOpenRaw() successful!\n")));
  return TRUE;
  }

//***************************************************************************

void CgebClose(CGOS_DRV_CGEB *cgeb)
  {
  if (cgeb->entry)
    CgebInvokePlain(cgeb,xCgebClose,NULL);

  if (cgeb->hiDescStart) {
    OsaUnMapAddress(cgeb->hiDescStart,cgeb->hiDescLen);
    cgeb->hiDescStart=NULL;
    }

  if (cgeb->mapMem) {
    CGEB_MAP_MEM_LIST *pmm;
    CGEB_MAP_MEM * pmme;
    unsigned long i;
    pmm=(CGEB_MAP_MEM_LIST *)cgeb->mapMem;
    pmme=pmm->entries;
    for (i=pmm->count; i; i--, pmme++) {
      if (pmme->virt.off) OsaUnMapAddress((void *)pmme->virt.off,pmme->size);
      pmme->virt.off=0;
      pmme->virt.seg=0;
      }
    }
  if (cgeb->data) {
    OsaMemFree(cgeb->data);
    cgeb->data=NULL;
    cgeb->ds=0;
    }
  if (cgeb->code) {
    OsaMemFree(cgeb->code);
    cgeb->code=NULL;
    }
  cgeb->entry=NULL;
  }

//***************************************************************************

unsigned int CgebOpen(CGOS_DRV_VARS *cdv, unsigned char *base, unsigned long len)
  {
  unsigned char *p,*pnext;
  CGOS_DRV_BOARD *brd=cdv->boards+cdv->boardCount;
  CGOS_DRV_CGEB *cgeb=&brd->cgeb;
  if (cdv->boardCount>=CGOS_DRV_BOARD_MAX) return FALSE;

  if (!len) {
    dbpf((TT("CGOS: Registering CGEB entry function at 0x%X\n"),(unsigned long)base));
    if (!CgebOpenRaw(brd,base,NULL,&pnext)) {
      CgebClose(cgeb);
      return FALSE;
      }
    cdv->boardCount++;
    return TRUE;
    }

  dbpf((TT("CGOS: Looking for CGEB lo desc between phys 0x%X and 0x%X\n"),(unsigned long)base,(unsigned long)base+len));
  p=(unsigned char *)OsaMapAddress((unsigned long)base,len);
  if (!p) return FALSE;
  if (!CgebOpenRaw(brd,p,p+len,&pnext)) {
    CgebClose(cgeb);
    OsaUnMapAddress(p,len);
    return FALSE;
    }

  OsaUnMapAddress(p,len);
  cdv->boardCount++;
  return TRUE;
  }

//***************************************************************************

unsigned long CgebTransAddr(CGOS_DRV_CGEB *cgeb, unsigned long addr)
  {
  switch (addr) {
    case -2: addr=(unsigned long)cgeb->code; break; // !!! 64
    case -3: addr=(unsigned long)cgeb->data; break;
    }
  return addr;
  }

//***************************************************************************
