#ifndef UNICORN_H
#define UNICORN_H

#include <linux/module.h>							/* Needed by all modules */
#include <linux/kernel.h>							/* Needed for KERN_INFO */
#include <linux/init.h>								/* Needed for the macros */
#include <linux/pci.h>
#include <linux/device.h>

#include <linux/proc_fs.h>							/* Necessary because we use the proc fs */
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-dma-contig.h>
#include <media/videobuf-core.h>

/********************************************************************************************************************/
/********************************************************************************************************************/

#define UNICORN_DEBUG_LEVEL 7
#define UNICORN_VIDEO_DEBUG_LEVEL 7

/********************************************************************************************************************/
#define UNICORN_VERSION_CODE KERNEL_VERSION(0, 0, 106)
#define VERSION_MAJOR_MIN 1
#define VERSION_MINOR_MIN 1
#define VERSION_MAJOR_NO_VERSION 255
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)        /* 0.5 seconds */



#define UNICORN_MAXBOARDS 2
#define RESOURCE_VIDEO0       1
#define RESOURCE_VIDEO1       2
#define MAX_WIDTH 2560
#define MAX_HEIGHT 1920
#define MIN_WIDTH 320
#define MIN_HEIGHT 240
#define MAX_DEPTH 16

#define AHB32_GLOBAL_REGISTERS			  0x00000000
#define AHB32_INTERRUPTS_CTRL			  0x00010000
#define AHB32_I2C_MASTER				  0x00020000
#define AHB32_SPI_FLASH					  0x00030000
#define AHB32_PCIE_GLOBAL_REGISTERS	      0x00040000
#define AHB32_PCIE_DMA					  0x00050000


#define IT_DMA_CHAN_0_TX_BUFF_0_END    (0x01 << 0)
#define IT_DMA_CHAN_0_TX_BUFF_1_END    (0x01 << 1)
#define IT_DMA_CHAN_0_ERROR            (0x01 << 2)
#define IT_DMA_CHAN_0_FIFO_FULL_ERROR  (0x01 << 3)

#define IT_DMA_CHAN_1_TX_BUFF_0_END    (0x01 << 4)
#define IT_DMA_CHAN_1_TX_BUFF_1_END    (0x01 << 5)
#define IT_DMA_CHAN_1_ERROR            (0x01 << 6)
#define IT_DMA_CHAN_1_FIFO_FULL_ERROR  (0x01 << 7)

#define IT_ABH32_ERROR                 (0x01 << 8)
#define IT_ABH32_FIFO_RX_ERROR         (0x01 << 9)
#define IT_ABH32_FIFO_TX_ERROR         (0x01 << 10)
#define IT_I2C_TRANSFER_END            (0x01 << 11)
#define IT_VIDEO_CHANNEL_0_OF_TRAME    (0x01 << 12)
#define IT_VIDEO_CHANNEL_1_OF_TRAME    (0x01 << 13)
#define IT_I2C_ACCES_ERROR             (0x01 << 16)
#define IT_I2C_WRITE_FIFO_ERROR        (0x01 << 17)
#define IT_I2C_READ_FIFO_ERROR         (0x01 << 18)
#define IT_RT                          (0x01 << 31)


#define AHB32_SPI_FLASH_REG_DATA					(AHB32_SPI_FLASH+0x0000)
/*BIT	Value	31..0	0	read_write_memory_ext	Out	data value*/

#define AHB32_SPI_FLASH_REG_CONTROL					(AHB32_SPI_FLASH+0x1000)
/*BIT	SECTOR_ADDRESS	10..0	0	read_write	Out	Sector address
BIT	START_CMD	11	0	write_pulse	Out	Start command access
BITVAL	IDLE	0x0
BITVAL	START	0x1
BIT	RNW_CMD	12	0	read_write	Out	Read/write access type selection
BITVAL	READ	0x1
BITVAL	WRITE	0x0
BIT	DATA_LENGTH	16..13	0	read_write	Out	SPI data phase length
BIT	ADDR_LENGTH	20..17	0	read_write	Out	SPI address phase length
BIT	ID_CODE	28..21	0	read_write	Out	SPI Flash ID code
BIT	Reserved	31..29	0	read_only
*/

#define AHB32_SPI_FLASH_REG_STATUS					(AHB32_SPI_FLASH+0x1004)
/*BIT	ERROR_CMD	0	0	read_clear	In	Acces error flag (not 32 bits aligned)
BITVAL	NOERROR	0x0
BITVAL	ERROR	0x1
BIT	BUSY_CMD	1	0	read_only	In	Acces in progess flag
BITVAL	IDLE	0x0
BITVAL	BUSY	0x1
BIT	Reserved	31..2	0	read_only
*/

#define AHB32_SPI_FLASH_REG_COMMAND_WDATA			(AHB32_SPI_FLASH+0x1010)
/*BIT	Value	31..0	0	read_write	Out	*/

#define AHB32_SPI_FLASH_REG_COMMAND_RDATA_MSB		(AHB32_SPI_FLASH+0x1020)
/*BIT	Value	31..0	0	read_only	In	*/

#define AHB32_SPI_FLASH_REG_COMMAND_RDATA_LSB		(AHB32_SPI_FLASH+0x1024)
/*BIT	Value	31..0	0	read_only	In	*/

#define RESET_ALL_VIDEO_INPUT 0x1F

/* cam_d.h end */

#define MAX_VID_CHANNEL_NUM 2
#define UNICORN_COMMAND_SIZE	1
#define MAX_I2C_ADAPTER 5
#define MAX_VIDEO_INPUT_ENTRY 5
#define MAX_SUBDEV_PER_VIDEO_BUS 2
#define MIRE_VIDEO_INPUT 4

#define CH00  0		/* Video 1 */
#define CH01  1		/* Video 2 */

#define DRV_NAME "unicorn"
#define UNICORN_PROCFILE_NAME	             DRV_NAME

#define PEGD_DEVICE_NAME 		     "Unicorn by Aldebaran Robotics"
#define PEGD_VENDOR_ID 		             PCI_VENDOR_ID_XILINX
#define PEGD_DEVICE_ID 			     0x0007
#define PEGD_SUBVENDOR_ID 		     0x8087
#define PEGD_SUBDEVICE_ID 		     0x3029


#define	UNICORN_BAR0	  		     0			/* PCIe base address */


#define UNICORN_DEFAULT_VIDEO_WIDTH	     0x280		/* 640 */
#define UNICORN_DEFAULT_VIDEO_HEIGHT	     0x1E0		/* 480 */

#define UNICORN_DMA_BLOC_SIZE	             1024

#define TIMEOUT_RETRY                        5
#define GET_TIMEOUT(TIMEOUT,CHANNEL)         ( (TIMEOUT & (0xF<<(CHANNEL*4))) >> (CHANNEL*4) )
#define RESET_TIMEOUT(TIMEOUT,CHANNEL)       ((TIMEOUT & ~(0xF<<(CHANNEL*4))))
#define INC_TIMEOUT(TIMEOUT,CHANNEL)         ( (( GET_TIMEOUT(TIMEOUT,CHANNEL) + 1)<<(CHANNEL*4)) + RESET_TIMEOUT(TIMEOUT,CHANNEL) )


struct buff_property {
  u32 addr;
  u32 size;
  u8 dummy[0x10 - 2*sizeof(u32)];
}__attribute__ ((packed));

struct dma_wr {
  struct buff_property buff[2];
  u32 ctrl;
  u8 dummy[0x100 - 2*sizeof(struct buff_property) - sizeof(u32)];
}__attribute__ ((packed));

struct  abh32_pcie_dma {
  struct dma_wr dma[2];
}__attribute__ ((packed));

struct video_in {
  u32 ctrl;
  u32 nb_lines;
  u32 nb_pixels;
  u32 nb_us_inhibit;
}__attribute__ ((packed));

struct unicorn_timeout_data {
  struct unicorn_dev * dev;
  struct dma_wr   * dma;
  struct video_in * video;
  struct unicorn_dmaqueue * vidq;
};

struct abh32_spi_flash
{
  u8 data[0x1000];
  u32 control;
  struct {
    u32 error:1;
    u32 busy:1;
    u32 reserved:30;
  }__attribute__ ((packed))status;
  u32 reserved[2];
  u32 cmd_wdata;
  u32 reserved2[3];
  u32 cmd_rdata_msb;
  u32 cmd_rdata_lsb;
}__attribute__((packed));

struct general {
  u32 id;
  struct option {
    u32 article:16;
    u32 functionnal:16;
  }__attribute__ ((packed)) option;
  struct version {
    u32 bugfix:8;
    u32 revision:8;
    u32 minor:8;
    u32 major:8;
  }__attribute__ ((packed)) version;
  u32 test;
}__attribute__ ((packed));

struct video_timestamp {
	u32 lsb;
	u32 msb;
}__attribute__ ((packed));

struct abh32_global_registers {
  struct general general;
  u32 video_in_reset;
  u8 dummy[0x20- sizeof(struct general) - sizeof(u32)];
  struct video_in video[2];
  struct video_timestamp video_timestamp[2];
}__attribute__ ((packed));

struct abh32_interrupts_controller {
  struct general general;
  struct {
    u32 ctrl;
    u32 pending;
  }__attribute__ ((packed)) irq;
}__attribute__ ((packed));

struct  abh32_i2c_master {
  u32 id;
  u32 option;
  u32 version;
  u32 control;

  struct status {
    u32 busy:1;
    u32 rd_fifo_full:1;
    u32 rd_fifo_empty:1;
    u32 wr_fifo_full:1;
    u32 wr_fifo_empty:1;
    u32 reserved:27;
  }__attribute__ ((packed)) status;
  u32 address;
  u32 subaddress;
  u32 wdata;
  u32 rdata;
  u32 burst_size;
}__attribute__ ((packed));

struct unicorn_fmt {
  char *name;
  u32 fourcc;		/* v4l2 format id */
  int depth;
  int flags;
  u32 cxformat;
};

/* buffer for one video frame */
struct unicorn_buffer {
  /* common v4l buffer stuff -- must be first */
  struct videobuf_buffer vb;

  /* unicorn specific */
  unsigned int bpl;

  struct unicorn_fmt *fmt;
  u32 count;
};

struct unicorn_fh {
  struct unicorn_dev *dev;
  enum v4l2_buf_type type;

  u32 resources;

  enum v4l2_priority prio;
  /* video capture */
  struct unicorn_fmt *fmt;
  unsigned int width, height;

  struct videobuf_queue vidq;
  struct videobuf_dma_contig_memory *dma_mem[VIDEO_MAX_FRAME];
  int channel;
  int input;
};

struct unicorn_dmaqueue {
  struct list_head active;
  struct list_head queued;
  struct timer_list timeout;
  u32 count;
  struct unicorn_fh * fh;
};

struct unicorn_dev
{
  volatile struct abh32_global_registers * global_register;
  volatile struct abh32_interrupts_controller * interrupts_controller;
  volatile struct abh32_pcie_dma * pcie_dma;
  volatile struct abh32_i2c_master * i2c_master;
  volatile struct abh32_spi_flash * spi_flash;

  /*V4L2 struct*/
  struct v4l2_subdev  *sensor[MAX_VIDEO_INPUT_ENTRY][MAX_SUBDEV_PER_VIDEO_BUS];
  struct video_device *video_dev[MAX_VID_CHANNEL_NUM];
  struct video_device *vbi_dev;
  struct video_device *radio_dev;
  struct video_device *ioctl_dev;
  struct v4l2_device v4l2_dev;
  struct v4l2_prio_state prio;

  struct i2c_adapter *i2c_adapter[MAX_I2C_ADAPTER];
  struct pci_dev *pci;

  struct unicorn_dmaqueue vidq[MAX_VID_CHANNEL_NUM];

  spinlock_t slock;
  struct mutex mutex;
  struct mutex i2c_mutex;
  volatile int i2c_error;
  volatile int i2c_eof;
  void *fpga_regs;
  int irq_number;

  struct class *class_unicorn;

  char command_buffer[UNICORN_COMMAND_SIZE+1];
  unsigned int pending;
  int interrupt_queue_flag;
  int camera;
  int stop;
  int frame_count;
  char name[32];
  int nr;
  int pixel_formats[MAX_VID_CHANNEL_NUM];
  int use_cif_resolution[MAX_VID_CHANNEL_NUM];
  int cif_width[MAX_VID_CHANNEL_NUM];
  int fps_limit[MAX_VID_CHANNEL_NUM]; // Limit the fps in FPGA. By default fps is limited by camera
  struct unicorn_timeout_data timeout_data[MAX_VID_CHANNEL_NUM];
  u32 resources;

  u8 fifo_full_error;
  u8 nb_timeout_fifo_full_error;

};

int unicorn_spi_flash_init(struct class *unicorn_class, struct unicorn_dev *dev);


#ifndef __UNICORN_VIDEO_C
extern unsigned int video_debug;
#else
unsigned int video_debug = 0;
#endif

#ifndef __UNICORN_CORE_C
extern unsigned int unicorn_debug;
#else
unsigned int unicorn_debug = 0;
#endif

#ifndef __UNICORN_CORE_C
extern unsigned int unicorn_version;
#else
unsigned int unicorn_version = 0;
#endif

#ifndef __UNICORN_VIDEO_C
extern unsigned int max_subdev_per_video_bus;
#else
unsigned int max_subdev_per_video_bus = 1;
#endif


#define dprintk(level, name,  fmt, arg...)\
    do { if (unicorn_debug >= level)\
  printk(KERN_DEBUG "%s/0: " fmt, name, ## arg);\
    } while (0)

#define dprintk_video(level, name, fmt, arg...)\
    do { if (video_debug >= level)\
  printk(KERN_DEBUG "%s/0: " fmt, name, ## arg);\
    } while (0)

#endif
