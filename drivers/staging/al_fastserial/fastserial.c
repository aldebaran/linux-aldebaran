/**
 * Driver for OXPCIe chip based PCIe extension (PCIe to serial bridge).
 *
 * Copyright (C) 2011 Aldebaran Robotics
 * joseph pinkasfeld joseph.pinkasfeld@gmail.com
 * Samuel MARTIN <s.martin49@gmail.com>
 *
 */
/**
 * Driver for Altera PCIe core chaining DMA reference design.
 *
 * Copyright (C) 2008 Leon Woestenberg  <leon.woestenberg@axon.tv>
 * Copyright (C) 2008 Nickolas Heppermann  <heppermannwdt@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * Rationale: This driver exercises the chaining DMA read and write engine
 * in the reference design. It is meant as a complementary reference
 * driver that can be used for testing early designs as well as a basis to
 * write your custom driver.
 *
 * Status: Test results from Leon Woestenberg  <leon.woestenberg@axon.tv>:
 *
 * Sendero Board w/ Cyclone II EP2C35F672C6N, PX1011A PCIe x1 PHY on a
 * Dell Precision 370 PC, x86, kernel 2.6.20 from Ubuntu 7.04.
 *
 * Sendero Board w/ Cyclone II EP2C35F672C6N, PX1011A PCIe x1 PHY on a
 * Freescale MPC8313E-RDB board, PowerPC, 2.6.24 w/ Freescale patches.
 *
 * Driver tests passed with PCIe Compiler 8.1. With PCIe 8.0 the DMA
 * loopback test had reproducable compare errors. I assume a change
 * in the compiler or reference design, but could not find evidence nor
 * documentation on a change or fix in that direction.
 *
 * The reference design does not have readable locations and thus a
 * dummy read, used to flush PCI posted writes, cannot be performed.
 *
 */

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#if 1
#define DEBUG_INTR(fmt...)	printk(fmt)
#else
#define DEBUG_INTR(fmt...)	do { } while (0)
#endif

/* by default do not build the character device interface */
/* XXX It is non-functional yet */
#ifndef FASTSERIAL_CDEV
#  define FASTSERIAL_CDEV 1
#endif

/* build the character device interface? */
#if FASTSERIAL_CDEV
#  define MAX_CHDMA_SIZE (8 * 1024 * 1024)
//#  include "mapper_user_to_sg.h"
#endif

/** driver name, mimicks Altera naming of the reference design */
#define DRV_NAME "fastserial"
/** number of BARs on the device */
#define APE_BAR_NUM (6)
/** BAR number where the RCSLAVE memory sits */
#define APE_BAR_RCSLAVE (0)
/** BAR number where the Descriptor Header sits */
#define APE_BAR_HEADER (2)

/** maximum size in bytes of the descriptor table, chdma logic limit */
#define APE_CHDMA_TABLE_SIZE (4096)
/* single transfer must not exceed 255 table entries. worst case this can be
 * achieved by 255 scattered pages, with only a single byte in the head and
 * tail pages. 253 * PAGE_SIZE is a safe upper bound for the transfer size.
 */
#define APE_CHDMA_MAX_TRANSFER_LEN (253 * PAGE_SIZE)


/**
 * Specifies those BARs to be mapped and the length of each mapping.
 *
 * Zero (0) means do not map, otherwise specifies the BAR lengths to be mapped.
 * If the actual BAR length is less, this is considered an error; then
 * reconfigure your PCIe core.
 *
 * @see ug_pci_express 8.0, table 7-2 at page 7-13.
 */
 //16k 2M 2M 0 0 0
static const unsigned long bar_min_len[APE_BAR_NUM] =
  { 16384, 2097152, 2097152, 0, 0, 0 };

/**
 * Descriptor Header, controls the DMA read engine or write engine.
 *
 * The descriptor header is the main data structure for starting DMA transfers.
 *
 * It sits in End Point (FPGA) memory BAR[2] for 32-bit or BAR[3:2] for 64-bit.
 * It references a descriptor table which exists in Root Complex (PC) memory.
 * Writing the rclast field starts the DMA operation, thus all other structures
 * and fields must be setup before doing so.
 *
 * @see ug_pci_express 8.0, tables 7-3, 7-4 and 7-5 at page 7-14.
 * @note This header must be written in four 32-bit (PCI DWORD) writes.
 */
struct ape_chdma_header {
  /**
   * w0 consists of two 16-bit fields:
   * lsb u16 number; number of descriptors in ape_chdma_table
   * msb u16 control; global control flags
   */
  u32 w0;
  /* bus address to ape_chdma_table in Root Complex memory */
  u32 bdt_addr_h;
  u32 bdt_addr_l;
  /**
   * w3 consists of two 16-bit fields:
   * - lsb u16 rclast; last descriptor number available in Root Complex
   *    - zero (0) means the first descriptor is ready,
   *    - one (1) means two descriptors are ready, etc.
   * - msb u16 reserved;
   *
   * @note writing to this memory location starts the DMA operation!
   */
  u32 w3;
} __attribute__ ((packed));

/**
 * Descriptor Entry, describing a (non-scattered) single memory block transfer.
 *
 * There is one descriptor for each memory block involved in the transfer, a
 * block being a contiguous address range on the bus.
 *
 * Multiple descriptors are chained by means of the ape_chdma_table data
 * structure.
 *
 * @see ug_pci_express 8.0, tables 7-6, 7-7 and 7-8 at page 7-14 and page 7-15.
 */
struct ape_chdma_desc {
  /**
   * w0 consists of two 16-bit fields:
   * number of DWORDS to transfer
   * - lsb u16 length;
   * global control
   * - msb u16 control;
   */
  u32 w0;
  /* address of memory in the End Point */
  u32 ep_addr;
  /* bus address of source or destination memory in the Root Complex */
  u32 rc_addr_h;
  u32 rc_addr_l;
} __attribute__ ((packed));

/**
 * Descriptor Table, an array of descriptors describing a chained transfer.
 *
 * An array of descriptors, preceded by workspace for the End Point.
 * It exists in Root Complex memory.
 *
 * The End Point can update its last completed descriptor number in the
 * eplast field if requested by setting the EPLAST_ENA bit either
 * globally in the header's or locally in any descriptor's control field.
 *
 * @note this structure may not exceed 4096 bytes. This results in a
 * maximum of 4096 / (4 * 4) - 1 = 255 descriptors per chained transfer.
 *
 * @see ug_pci_express 8.0, tables 7-9, 7-10 and 7-11 at page 7-17 and page 7-18.
 */
struct ape_chdma_table {
  /* workspace 0x00-0x0b, reserved */
  u32 reserved1[3];
  /* workspace 0x0c-0x0f, last descriptor handled by End Point */
  u32 w3;
  /* the actual array of descriptors
    * 0x10-0x1f, 0x20-0x2f, ... 0xff0-0xfff (255 entries)
    */
  struct ape_chdma_desc desc[255];
} __attribute__ ((packed));
/**
 * Altera PCI Express ('ape') board specific book keeping data
 *
 * Keeps state of the PCIe core and the Chaining DMA controller
 * application.
 */
struct ape_dev {
  /** the kernel pci device data structure provided by probe() */
  struct pci_dev *pci_dev;
  /**
   * kernel virtual address of the mapped BAR memory and IO regions of
   * the End Point. Used by map_bars()/unmap_bars().
   */
  void * __iomem bar[APE_BAR_NUM];
  /** kernel virtual address for Descriptor Table in Root Complex memory */
  struct ape_chdma_table *table_virt;
  /**
   * bus address for the Descriptor Table in Root Complex memory, in
   * CPU-native endianess
   */
  dma_addr_t table_bus;
  /* if the device regions could not be allocated, assume and remember it
   * is in use by another driver; this driver must not disable the device.
   */
  int in_use;
  /* whether this driver enabled msi for the device */
  int msi_enabled;
  /* whether this driver could obtain the regions */
  int got_regions;
  /* irq line succesfully requested by this driver, -1 otherwise */
  int irq_line;
  /* board revision */
  u8 revision;
  /* interrupt count, incremented by the interrupt handler */
  int irq_count;
#if FASTSERIAL_CDEV
  /* character device */
  dev_t cdevno;
  struct cdev cdev;
  /* user space scatter gather mapper */
  struct sg_mapping_t *sgm;
  /* virtual address for buffer for DMA transfert */
  /* hard address for buffer for DMA transfert   */
  unsigned char *buffer_virt;
  dma_addr_t buffer_bus;
#endif
};

/**
 * Using the subsystem vendor id and subsystem id, it is possible to
 * distinguish between different cards bases around the same
 * (third-party) logic core.
 *
 * Default Altera vendor and device ID's, and some (non-reserved)
 * ID's are now used here that are used amongst the testers/developers.
 */

static const struct pci_device_id ids[] = {
  { PCI_DEVICE(PCI_VENDOR_ID_OXSEMI, 0xc208), },
  { PCI_DEVICE(PCI_VENDOR_ID_OXSEMI, 0xc20d), },
  { 0, }
};
MODULE_DEVICE_TABLE(pci, ids);

#if FASTSERIAL_CDEV
/* prototypes for character device */
static int sg_init(struct ape_dev *ape);
static void sg_exit(struct ape_dev *ape);
#endif





#define UART_16C950_DLL       (0x00)
#define UART_16C950_DLM       (0x01)
#define UART_16C950_FCR       (0x02)
#define UART_16C950_LCR       (0x03)
#define UART_16C950_MCR       (0x04)
#define UART_16C950_ICR        (0xC0)
#define UART_16C950_CSR       (UART_16C950_ICR + 0x0C)
#define UART_16C950_ACR      (UART_16C950_ICR )
#define UART_16C950_CPR      (UART_16C950_ICR +0x01)
#define UART_16C950_TCR      (UART_16C950_ICR +0x02)
#define UART_16C950_CPR2    (UART_16C950_ICR +0x03)
#define UART_16C950_TTL       (UART_16C950_ICR +0x04)
#define UART_16C950_RTL       (UART_16C950_ICR +0x05)
#define UART_16C950_NMR       (UART_16C950_ICR +0x0D)
#define UART_16C950_MDM      (UART_16C950_ICR +0x0E)
#define UART_16C950_EFR       (0x02)
#define UART_16C950_IER       (0x01)
#define UART_16C950_DMA_ADDRESS (0x100)
#define UART_16C950_DMA_ADDRESS64bit (0x104)
#define UART_16C950_PIDX      (UART_16C950_ICR +0x12)
void init_serial_port(struct ape_dev *ape, int idx)
{
  u8 __iomem *p = ape->bar[0] + (unsigned int)idx * 0x100 * 2 + 0x1000;

  /* enhanced mode  */
  iowrite8(0xBF,p + UART_16C950_LCR);
  iowrite8(0x10,p + UART_16C950_EFR);
  iowrite8(0x00,p + UART_16C950_LCR);

    /* reset UART */
  iowrite8(0x00,p + UART_16C950_CSR);
  iowrite8(0xBF,p + UART_16C950_LCR);
  iowrite8(0x10,p + UART_16C950_EFR);
  iowrite8(0x00,p + UART_16C950_LCR);

  /* activate 128 bytes FIFO*/
  iowrite8(0x01,p +  UART_16C950_FCR);
  /* flush FIFO and activate DMA MODE 1*/
  iowrite8(0x07+0x08,p +  UART_16C950_FCR);
  /* activate custom interrupt FIFO trigger level */
  /* activate DTR control for RS485 active high */
  iowrite8(0x38,p + UART_16C950_ACR);
  /* 1 byte in FIFO trigger interrupt */
  iowrite8(0x01,p + UART_16C950_TTL);
  /* 125 byte in FIFO trigger interrupt */
  iowrite8(0x8C,p + UART_16C950_RTL);
  /* fix oversample to 8 */
  iowrite8(0x04,p + UART_16C950_TCR);
  /* Fix speed to 921600 bauds */
  iowrite8(0x08,p + UART_16C950_CPR);
    /* Fix speed to 921600 bauds */
  iowrite8(0x80,p + UART_16C950_MCR);

  iowrite8(0x00,p + UART_16C950_CPR2);
  iowrite8(0xBF,p + UART_16C950_LCR);
  iowrite8(0x0F,p + UART_16C950_DLL);
  iowrite8(0x00,p + UART_16C950_DLM);
  iowrite8(0x00,p + UART_16C950_LCR);
  /* Enable 9bit mode */
  iowrite8(0x01,p + UART_16C950_NMR);
  /* Disable modem interrupt */
  iowrite8(0x0F,p + UART_16C950_MDM);
  /* Disable all interupts*/
  iowrite8(0x00,p + UART_16C950_IER);
  printk(KERN_DEBUG "fastSerial : port %d initialised at %p \n",ioread8(p+UART_16C950_PIDX),p);
  if(ape->buffer_bus)
  {
    iowrite32(ape->buffer_bus, p + UART_16C950_DMA_ADDRESS);
    iowrite32(0, p + UART_16C950_DMA_ADDRESS64bit);
  }
}

static int scan_bars(struct ape_dev *ape, struct pci_dev *dev)
{
  int i;
  for (i = 0; i < APE_BAR_NUM; i++) {
    unsigned long bar_start = pci_resource_start(dev, i);
    if (bar_start) {
      unsigned long bar_end = pci_resource_end(dev, i);
      unsigned long bar_flags = pci_resource_flags(dev, i);
      printk(KERN_DEBUG "BAR%d 0x%08lx-0x%08lx flags 0x%08lx\n",
        i, bar_start, bar_end, bar_flags);
    }
  }
  return 0;
}

/**
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct ape_dev *ape, struct pci_dev *dev)
{
  int i;
  for (i = 0; i < APE_BAR_NUM; i++) {
    /* is this BAR mapped? */
    if (ape->bar[i]) {
      /* unmap BAR */
      pci_iounmap(dev, ape->bar[i]);
      ape->bar[i] = NULL;
    }
  }
}

/**
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed, given by the
 * bar_min_len[] array.
 */
static int map_bars(struct ape_dev *ape, struct pci_dev *dev)
{
  int rc;
  int i;
  /* iterate through all the BARs */
  for (i = 0; i < APE_BAR_NUM; i++) {
    unsigned long bar_start = pci_resource_start(dev, i);
    unsigned long bar_end = pci_resource_end(dev, i);
    unsigned long bar_length = bar_end - bar_start + 1;
    ape->bar[i] = NULL;
    /* do not map, and skip, BARs with length 0 */
    if (!bar_min_len[i])
      continue;
    /* do not map BARs with address 0 */
    if (!bar_start || !bar_end) {
      printk(KERN_DEBUG "BAR #%d is not present?!\n", i);
      rc = -1;
      goto fail;
    }
    bar_length = bar_end - bar_start + 1;
    /* BAR length is less than driver requires? */
    if (bar_length < bar_min_len[i]) {
      printk(KERN_DEBUG "BAR #%d length = %lu bytes but driver "
      "requires at least %lu bytes\n",
      i, bar_length, bar_min_len[i]);
      rc = -1;
      goto fail;
    }
    /* map the device memory or IO region into kernel virtual
     * address space */
    ape->bar[i] = pci_iomap(dev, i, bar_min_len[i]);
    if (!ape->bar[i]) {
      printk(KERN_DEBUG "Could not map BAR #%d.\n", i);
      rc = -1;
      goto fail;
    }
    printk(KERN_DEBUG "BAR[%d] mapped at 0x%p with length %lu(/%lu).\n", i,
    ape->bar[i], bar_min_len[i], bar_length);
  }
  /* succesfully mapped all required BAR regions */
  rc = 0;
  goto success;
fail:
  /* unmap any BARs that we did map */
  unmap_bars(ape, dev);
success:
  return rc;
}

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define pci_dma_h(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define pci_dma_l(addr) (addr & 0xffffffffUL)

/* ape_fill_chdma_desc() - Fill a Altera PCI Express Chaining DMA descriptor
 *
 * @desc pointer to descriptor to be filled
 * @addr root complex address
 * @ep_addr end point address
 * @len number of bytes, must be a multiple of 4.
 */
static inline void ape_chdma_desc_set(struct ape_chdma_desc *desc, dma_addr_t addr, u32 ep_addr, int len)
{
  BUG_ON(len & 3);
  desc->w0 = cpu_to_le32(len / 4);
  desc->ep_addr = cpu_to_le32(ep_addr);
  desc->rc_addr_h = cpu_to_le32(pci_dma_h(addr));
  desc->rc_addr_l = cpu_to_le32(pci_dma_l(addr));
}



/* compare buffers */
static inline int compare(u32 *p, u32 *q, int len)
{
  int result = -1;
  int fail = 0;
  int i;
  for (i = 0; i < len / 4; i++) {
    if (*p == *q) {
      /* every so many u32 words, show equals */
      if ((i & 255) == 0)
        printk(KERN_DEBUG "[%p] = 0x%08x    [%p] = 0x%08x\n", p, *p, q, *q);
    } else {
      fail++;
      /* show the first few miscompares */
      if (fail < 10)
        printk(KERN_DEBUG "[%p] = 0x%08x != [%p] = 0x%08x ?!\n", p, *p, q, *q);
        /* but stop after a while */
      else if (fail == 10)
        printk(KERN_DEBUG "---more errors follow! not printed---\n");
      else
        /* stop compare after this many errors */
      break;
    }
    p++;
    q++;
  }
  if (!fail)
    result = 0;
  return result;
}


/* Called when the PCI sub system thinks we can control the given device.
 * Inspect if we can support the device and if so take control of it.
 *
 * Return 0 when we have taken control of the given device.
 *
 * - allocate board specific bookkeeping
 * - allocate coherently-mapped memory for the descriptor table
 * - enable the board
 * - verify board revision
 * - request regions
 * - query DMA mask
 * - obtain and request irq
 * - map regions into kernel address space
 */
static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
  int rc = 0;
  int portIdx=0;
  int nbPort=0;
  struct ape_dev *ape = NULL;
  u8 irq_pin, irq_line;
  printk(KERN_DEBUG "probe(dev = 0x%p, pciid = 0x%p)\n", dev, id);

  /* allocate memory for per-board book keeping */
  ape = kzalloc(sizeof(struct ape_dev), GFP_KERNEL);
  if (!ape) {
    printk(KERN_DEBUG "Could not kzalloc()ate memory.\n");
    goto err_ape;
  }
  ape->pci_dev = dev;
  dev_set_drvdata(&dev->dev, ape);
  printk(KERN_DEBUG "probe() ape = 0x%p\n", ape);

//	printk(KERN_DEBUG "sizeof(struct ape_chdma_table) = %d.\n",
  //  (int)sizeof(struct ape_chdma_table));
  /* the reference design has a size restriction on the table size */
  BUG_ON(sizeof(struct ape_chdma_table) > APE_CHDMA_TABLE_SIZE);

  /* allocate and map coherently-cached memory for a descriptor table */
  /* @see LDD3 page 446 */
  ape->table_virt = (struct ape_chdma_table *)pci_alloc_consistent(dev,
    APE_CHDMA_TABLE_SIZE, &ape->table_bus);
  /* could not allocate table? */
  if (!ape->table_virt) {
    printk(KERN_DEBUG "Could not dma_alloc()ate_coherent memory.\n");
    goto err_table;
  }

  printk(KERN_DEBUG "table_virt = %p, table_bus = 0x%16llx.\n",
    ape->table_virt, (u64)ape->table_bus);

  /* enable device */
  rc = pci_enable_device(dev);
  if (rc) {
    printk(KERN_DEBUG "pci_enable_device() failed\n");
    goto err_enable;
  }

  /* enable bus master capability on device */
  pci_set_master(dev);
  /* enable message signaled interrupts */
  rc = pci_enable_msi(dev);
  /* could not use MSI? */
  if (rc) {
    /* resort to legacy interrupts */
    printk(KERN_DEBUG "Could not enable MSI interrupting.\n");
    ape->msi_enabled = 0;
  /* MSI enabled, remember for cleanup */
  } else {
    printk(KERN_DEBUG "Enabled MSI interrupting.\n");
    ape->msi_enabled = 1;
  }

  pci_read_config_byte(dev, PCI_REVISION_ID, &ape->revision);
#if 0 /* example */
  /* (for example) this driver does not support revision 0x42 */
    if (ape->revision == 0x42) {
    printk(KERN_DEBUG "Revision 0x42 is not supported by this driver.\n");
    rc = -ENODEV;
    goto err_rev;
  }
#endif
  /** XXX check for native or legacy PCIe endpoint? */

  rc = pci_request_regions(dev, DRV_NAME);
  /* could not request all regions? */
  if (rc) {
    /* assume device is in use (and do not disable it later!) */
    ape->in_use = 1;
    goto err_regions;
  }
  ape->got_regions = 1;

#if 1   /* @todo For now, disable 64-bit, because I do not understand the implications (DAC!) */
  /* query for DMA transfer */
  /* @see Documentation/PCI/PCI-DMA-mapping.txt */
  if (!pci_set_dma_mask(dev, DMA_BIT_MASK(64))) {
    pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(64));
    /* use 64-bit DMA */
    printk(KERN_DEBUG "Using a 64-bit DMA mask.\n");
  } else
#endif
  if (!pci_set_dma_mask(dev, DMA_BIT_MASK(32))) {
    printk(KERN_DEBUG "Could not set 64-bit DMA mask.\n");
    pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(32));
    /* use 32-bit DMA */
    printk(KERN_DEBUG "Using a 32-bit DMA mask.\n");
  } else {
    printk(KERN_DEBUG "No suitable DMA possible.\n");
    /** @todo Choose proper error return code */
    rc = -1;
    goto err_mask;
  }

  rc = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq_pin);
  /* could not read? */
  if (rc)
    goto err_irq;
  printk(KERN_DEBUG "IRQ pin #%d (0=none, 1=INTA#...4=INTD#).\n", irq_pin);

  /* @see LDD3, page 318 */
  rc = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq_line);
  /* could not read? */
  if (rc) {
    printk(KERN_DEBUG "Could not query PCI_INTERRUPT_LINE, error %d\n", rc);
    goto err_irq;
  }
  printk(KERN_DEBUG "IRQ line #%d.\n", irq_line);
#if 1
  irq_line = dev->irq;
  /* remember which irq we allocated */
  ape->irq_line = (int)irq_line;
  printk(KERN_DEBUG "Succesfully requested IRQ #%d with dev_id 0x%p\n", irq_line, ape);
#endif
  /* show BARs */
  scan_bars(ape, dev);
  /* map BARs */
  rc = map_bars(ape, dev);
  if (rc)
    goto err_map;
#if FASTSERIAL_CDEV
  /* initialize character device */
  rc = sg_init(ape);
  if (rc)
    goto err_cdev;

  /* perform DMA engines loop back test */
//rc = dma_test(ape, dev);
#endif
  (void)rc;
  /* succesfully took the device */
  rc = 0;
  printk(KERN_DEBUG "probe() successful.\n");
  /* read number of serial port available */
  nbPort = ioread8(ape->bar[0] + 4);
  printk(KERN_DEBUG "fastSerial : %d port available \n", nbPort);
  for (portIdx=0;portIdx<nbPort;portIdx++){
    init_serial_port(ape, portIdx);
  }

  goto end;
#if FASTSERIAL_CDEV
err_cdev:
  /* unmap the BARs */
  unmap_bars(ape, dev);
#endif
err_map:
  /* free allocated irq */
  if (ape->irq_line >= 0)
    free_irq(ape->irq_line, (void *)ape);
err_irq:
  if (ape->msi_enabled)
    pci_disable_msi(dev);
  /* disable the device iff it is not in use */
  if (!ape->in_use)
    pci_disable_device(dev);
  if (ape->got_regions)
    pci_release_regions(dev);
err_mask:
err_regions:
/*err_rev:*/
/* clean up everything before device enable() */
err_enable:
  if (ape->table_virt)
    pci_free_consistent(dev, APE_CHDMA_TABLE_SIZE, ape->table_virt, ape->table_bus);
/* clean up everything before allocating descriptor table */
err_table:
  if (ape)
    kfree(ape);
err_ape:
end:
  return rc;
}

static void remove(struct pci_dev *dev)
{
  struct ape_dev *ape = dev_get_drvdata(&dev->dev);

  printk(KERN_DEBUG "remove(0x%p)\n", dev);
  printk(KERN_DEBUG "remove(dev = 0x%p) where ape = 0x%p\n", dev, ape);

  /* remove character device */
#if FASTSERIAL_CDEV
  sg_exit(ape);
#endif

  if (ape->table_virt)
    pci_free_consistent(dev, APE_CHDMA_TABLE_SIZE, ape->table_virt, ape->table_bus);

  /* free IRQ
   * @see LDD3 page 279
   */
  if (ape->irq_line >= 0) {
    printk(KERN_DEBUG "Freeing IRQ #%d for dev_id 0x%08lx.\n",
    ape->irq_line, (unsigned long)ape);
    free_irq(ape->irq_line, (void *)ape);
  }
  /* MSI was enabled? */
  if (ape->msi_enabled) {
    /* Disable MSI @see Documentation/MSI-HOWTO.txt */
    pci_disable_msi(dev);
    ape->msi_enabled = 0;
  }
  /* unmap the BARs */
  unmap_bars(ape, dev);
  if (!ape->in_use)
    pci_disable_device(dev);
  if (ape->got_regions)
    /* to be called after device disable */
    pci_release_regions(dev);
}

#if FASTSERIAL_CDEV

/*
 * Called when the device goes from unused to used.
 */
static int sg_open(struct inode *inode, struct file *file)
{
  struct ape_dev *ape;
  int portIdx;
  int nbPort = 4;
//  printk(KERN_DEBUG DRV_NAME "_open()\n");
  /* pointer to containing data structure of the character device inode */
  ape = container_of(inode->i_cdev, struct ape_dev, cdev);
  /* create a reference to our device state in the opened file */
  file->private_data = ape;
  /* create virtual memory mapper */
  //ape->sgm = sg_create_mapper(MAX_CHDMA_SIZE);
  for (portIdx=0;portIdx<nbPort;portIdx++){
    init_serial_port(ape,portIdx);
  }
  return 0;
}

/*
 * Called when the device goes from used to unused.
 */
static int sg_close(struct inode *inode, struct file *file)
{
  /* fetch device specific data stored earlier during open */
  //struct ape_dev *ape = (struct ape_dev *)file->private_data;
 // printk(KERN_DEBUG DRV_NAME "_close()\n");
  /* destroy virtual memory mapper */
  //sg_destroy_mapper(ape->sgm);
  return 0;
}

const unsigned short int CCITTtable[] = {
0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

u16 crcCCITT (u16 crc_init, const u8 data[], u16 size)
{
  u16 i = 0;

  for (i = 0; i < size; i++){
    crc_init = (unsigned short int)((crc_init << 8) ^ CCITTtable[(crc_init >> 8) ^ data[i]]);
  }
  return crc_init;
}

int checkUnk(u8 *data,int size){
  u16 crc,receivedCRC;

  crc = crcCCITT(0, data,size-2);
  receivedCRC = (u16)data[size-2] | (u16)(data[size-1]<<8);
  if (crc != receivedCRC){
    return 0;
  }
  else {
    return 1;
  }
}

#define UART_16C950_950_SPEC                 (0xA0)
#define UART_16C950_RFL                           (UART_16C950_950_SPEC + 0x03)
#define UART_16C950_DMA_STATUS            (0x10C)
#define UART_16C950_DMA_LENGTH           (0x0108)
#define UART_16C950_THR                   (0x80)
#define NACK (0x80)
#define UART_16C950_RX                    (0x00)
#define UART_16C950_LSR                    (0x05)
#define UART_16C950_LSR_9TH_MASK           (0x04)
#define CRC_LENGTH                          (2)
#define COMMAND_LENGTH                      (1)
#define MASK_SIZE 0x0070
static ssize_t sg_read(struct file *file, char __user *buf, size_t count, loff_t *pos){
  u8 __iomem *p;
  static int cycle = 0;
  int totalSize = 0;
  int i,j,k;
  int length=0;
  char * rxbuf;
  char * dmabuf;
  // skip 8 first byte with length of each buffer
  int rxbuffposition=8;
  struct ape_dev *ape = (struct ape_dev *)file->private_data;

//  printk(KERN_DEBUG DRV_NAME "_read(buf=0x%p, count=%lld, pos=%llu)\n", buf, (s64)count, (u64)*pos);

  rxbuf = buf + rxbuffposition;
  ape = (struct ape_dev *)file->private_data;
  for (i=0; i< 8; i++) {
    p = ape->bar[0] + (unsigned int)i * 0x100 *2 + 0x1000;
    length = ioread8(p + UART_16C950_RFL);
    if (length) {
      if(0x01 & ioread8(p + UART_16C950_DMA_STATUS)) {
        printk(KERN_ERR DRV_NAME "_sg_read DMA ERROR\n");
        iowrite8(0,p + UART_16C950_DMA_STATUS);
      }
      iowrite32(0x80000000 + length, p + UART_16C950_DMA_LENGTH);
      // wait 100 cycle for DMA to finish
      for (j=0;j<100;j++) {
        if(ioread8(p + UART_16C950_DMA_STATUS) & 0x04) {
          if (j>10) {
           printk(KERN_ERR DRV_NAME " wait %d cycle before receiving\n",j);
           return 0;
          }
          break;
        }
      }

      dmabuf = ape->buffer_virt;

      // check 9th bit
      buf[i] = 0;
      k = 0;
      for(k=0;k<length/(count+ CRC_LENGTH + COMMAND_LENGTH);k++){
        if(checkUnk(dmabuf,count + CRC_LENGTH + COMMAND_LENGTH )){
          memcpy(rxbuf,dmabuf,count + COMMAND_LENGTH);
          rxbuf  += count + COMMAND_LENGTH; // inc buffer position
          buf[i] += count + COMMAND_LENGTH; // inc total reception size
          dmabuf += count + CRC_LENGTH + COMMAND_LENGTH;
        }
        else{
          printk(KERN_DEBUG DRV_NAME " length %d Wrong CRC on card %d bus %d cycle %d\n",length,dmabuf[0] & 0x0F,i,cycle);
        }
      }
    }
    totalSize += length;
  }
  cycle++;
  return 1;
}



/* sg_write() - Write to the device
 *
 * @buf userspace buffer
 * @count number of bytes in the userspace buffer
 *
 * Iterate over the userspace buffer, taking at most 255 * PAGE_SIZE bytes for
 * each DMA transfer.
 *   For each transfer, get the user pages, build a sglist, map, build a
 *   descriptor table. submit the transfer. wait for the interrupt handler
 *   to wake us on completion.
 */
#define UART_16C950_SCR (7)
#define UART_16C950_TX (0)
#define UART_16C950_DMA_STATE (0x10C)
#define MASK_READ 0x0080

static ssize_t sg_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
  u8 __iomem *p;
  int i,j;
  u16 crc;
  int length=0;
  const u8 * txbuf;
  // skip 8 first byte with length of each buffer
  int txbuffposition=8;
  struct ape_dev *ape;
  //printk(KERN_DEBUG DRV_NAME "_sg_write()\n");

  ape = (struct ape_dev *)file->private_data;
  for (i=0; i< 8; i++)
  {
    p = ape->bar[0] + (unsigned int)i * 0x100 *2 + 0x1000;
    length = buf[i]; // first byte is sent alone
    txbuf = buf + txbuffposition ;
    if(length)
    {
      // enable 9th bit
      iowrite8(0x01,p + UART_16C950_SCR);
      // send broadcast start
      iowrite8(txbuf[0],p + UART_16C950_TX);
      // disable 9th bit
      iowrite8(0x00,p + UART_16C950_SCR);
      crc = crcCCITT(0,txbuf,1);
      if(txbuf[0] & MASK_READ) { /*read marker ADD CRC to command*/
        iowrite8(crc & 0xFF, p + UART_16C950_TX);
        iowrite8((crc >> 8) & 0xFF, p + UART_16C950_TX);
      }
      length -=1;
      txbuffposition += 1;
    }
    if(length)
    {
      txbuf = buf + txbuffposition ;
      for(j=0;j<length/4;j++)
      {
          iowrite32(((u32*)(txbuf))[j],p + UART_16C950_THR);
      }
      crc = crcCCITT(crc,txbuf,length);
      //crc  = ( crc << 8 ) + (crc >> 8);
      iowrite16(crc,p + UART_16C950_THR);
      txbuffposition += length;
    }
  }
  return count;
}

/*
 * character device file operations
 */
static const struct file_operations sg_fops = {
  .owner = THIS_MODULE,
  .open = sg_open,
  .release = sg_close,
  .read = sg_read,
  .write = sg_write,
};

/**
 * sg_init() - Initialize character device
 */
static int sg_init(struct ape_dev *ape)
{
  int rc;
  printk(KERN_DEBUG DRV_NAME " sg_init()\n");
  /* allocate a dynamically allocated character device node */
  rc = alloc_chrdev_region(&ape->cdevno, 0/*requested minor*/, 1/*count*/, DRV_NAME);
  /* allocation failed? */
  if (rc < 0) {
    printk("alloc_chrdev_region() = %d\n", rc);
    goto fail_alloc;
  }
  /* couple the device file operations to the character device */
  cdev_init(&ape->cdev, &sg_fops);
  ape->cdev.owner = THIS_MODULE;
  /* bring character device live */
  rc = cdev_add(&ape->cdev, ape->cdevno, 1/*count*/);
  if (rc < 0) {
    printk("cdev_add() = %d\n", rc);
    goto fail_add;
  }
  ape->buffer_virt = (unsigned char *)pci_alloc_consistent(ape->pci_dev, 128, &ape->buffer_bus);
  if (! ape->buffer_virt) {
    printk("Could not allocate coherent DMA buffer.\n");
  }


  printk("Allocated cache-coherent DMA buffer (virtual address = %p, bus address = 0x%016llx).\n",
          ape->buffer_virt, (u64)ape->buffer_bus);
  printk(KERN_DEBUG "fastserial = %d:%d\n", MAJOR(ape->cdevno), MINOR(ape->cdevno));
  return 0;
fail_add:
  /* free the dynamically allocated character device node */
    unregister_chrdev_region(ape->cdevno, 1/*count*/);
fail_alloc:
  return -1;
}

/* sg_exit() - Cleanup character device
 *
 * XXX Should ideally be tied to the device, on device remove, not module exit.
 */
static void sg_exit(struct ape_dev *ape)
{
  printk(KERN_DEBUG DRV_NAME " sg_exit()\n");
  /* remove the character device */
  cdev_del(&ape->cdev);
  /* free the dynamically allocated character device node */
  unregister_chrdev_region(ape->cdevno, 1/*count*/);
}

#endif /* FASTSERIAL_CDEV */

/* used to register the driver with the PCI kernel sub system
 * @see LDD3 page 311
 */
static struct pci_driver pci_driver = {
  .name = DRV_NAME,
  .id_table = ids,
  .probe = probe,
  .remove = remove,
  /* resume, suspend are optional */
};

/**
 * fastserial_init() - Module initialization, registers devices.
 */
static int __init fastserial_init(void)
{
  int rc = 0;
  printk(KERN_DEBUG DRV_NAME " init()\n");
  /* register this driver with the PCI bus driver */
  rc = pci_register_driver(&pci_driver);
  if (rc < 0)
    return rc;
  return 0;
}

/**
 * fastserial_exit() - Module cleanup, unregisters devices.
 */
static void __exit fastserial_exit(void)
{
  printk(KERN_DEBUG DRV_NAME " exit(), built at \n");
  /* unregister this driver from the PCI bus driver */
  pci_unregister_driver(&pci_driver);
}

MODULE_LICENSE("GPL");

module_init(fastserial_init);
module_exit(fastserial_exit);
