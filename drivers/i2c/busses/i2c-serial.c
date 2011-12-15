/*
  i2c-serial.c - I2C Driver for I2C on serial port
  Copyright (c) 2007  Momtchil Momtchev <momtchil@momtchev.com>
  Based on code written by Ralph Metzler <rjkm@thp.uni-koeln.de> and
  Simon Vogl

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


/* This interfaces to the I2C bus connected to a serial port */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/tty.h>
#include <linux/poll.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>

#define I2CSERIAL_DEBUG 0

#if I2CSERIAL_DEBUG==1
#define VERSION "1.15d"
#else
#define VERSION "1.15"
#endif

#define I2CSERIAL_STATS

/*
 * History
 * v1.00 by Momtchil
 * v1.01 (and following) by Brice Marnier <marnier@aldebaran-robotics.com>
 *  adapted to match new PSoC protocol
 * v1.03 - support for PSoC Watchdog reset
 * v1.04 - speed enhancement
 * v1.05 - i2c_transfer support, firmware version display at startup
 * v1.06 - escape sequence bugfix
 * v1.07 - added consistent error codes
 *         timeout down to 5ms
 * v1.08 - reduced logging in potentially repetitive error cases
 * v1.10 - /proc/i2cstats version info and error statistics
 * v1.11 - timeout = 52 (make the flasher work)
 * v1.12 - send reset to the psoc in i2c mode too
 * v1.13 - handle wakeup order from the psoc
 *       - cedric gestes <gestes@aldebaran-robotics.com>
 * v1.14 - handle i2c_smbus_read_i2c_block_data
 *       - code factorisation
 *       - cedric gestes <gestes@aldebaran-robotics.com>
 * v1.15 - init return
 * todo    (one day /sys/... equivalent ?)
 *  */

#define DRV_NAME	"Serial I2C/SMBus driver"
const char drv_name[] = DRV_NAME;

MODULE_AUTHOR("Momtchil Momtchev <momtchil@momtchev.com>, Brice Marnier <briareos@hecatonchires.com>");
MODULE_DESCRIPTION(DRV_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);

#define TIMEOUT                 msecs_to_jiffies(52)
//#define TIMEOUT_RESET		msecs_to_jiffies(50)
#define I2C_SERIAL_BUFFER_LENGTH 256

#include "psoc-protocol.h"

/* PSoC is idle */
#define I2C_STATE_IDLE	0x01
/* Sending on serial */
#define I2C_STATE_SEND	0x02
/* Waiting for reply, probably [Esc]*/
#define I2C_STATE_WAIT  0x04
/* Waiting for raw input data or [Esc]*/
#define I2C_STATE_READ	0x06
/* Waiting for input after ESCAPE_CHAR */
#define I2C_STATE_CMD	0x08
/* Waiting for watchdog reset, i.e. [Esc]'z' after ~8ms idle*/
/* WAIT_WATCHDOG is 0x10 + IDLE, to be detected as an idle mode */
#define I2C_STATE_WAIT_WATCHDOG	0x11
/* Version request running = waiting for constant length string reply */
#define I2C_STATE_VERSION 0x20

#define I2C_ERROR_NAK      (   -EAGAIN) // "Ressource temporarily unavailable"
#define I2C_ERROR_ADDRNAK  (    -ENXIO) // "No such device or address"
#define I2C_ERROR_BUSY     (    -EBUSY) // "Device or resource busy"
#define I2C_ERROR_STALL    (      -EIO) // "Input/output error"
#define I2C_ERROR_TIMEOUT  (-ETIMEDOUT) // "Connection timed out"
#define I2C_ERROR_OVERBUFF (-EOVERFLOW) // "Value too large..."
#define I2C_ERROR_RESET    (-ECANCELED) // "Operation canceled"
#define I2C_ERROR_MTF      (-ENOMEDIUM)

static DECLARE_WAIT_QUEUE_HEAD(wq);


/**
 * 2.6.22.9 hacked to support read with size diff than 32
 * in newer kernel 2.6.23 or later
 * the previous I2C_SMBUS_I2C_BLOCK_DATA has been renamed
 * I2C_SMBUS_I2C_BLOCK_BROKEN
 *
 * but I dont want to make that change here
 * CTAF
 */
#define I2C_SMBUS_I2C_BLOCK_DATA_ALL_SIZE 8




/* {{{ i2c_data
 * structure for I2C-serial data
 * */
struct i2c_data {
  struct i2c_adapter adapter;
  struct i2c_client *client;
  int state;
  int error;
  int jiffies;
  unsigned char * irq_buffer; /* can point either to buffer below, or to provided buffer for I2C transfers */
  unsigned char buffer[I2C_SERIAL_BUFFER_LENGTH];
  unsigned int pos; /* position inside the buffer */
  u8 reply_count;      /* number of replies to wait for before command completion */
  // u8 addr;             /* last used address  (useless) */
}; // }}}

#ifdef I2CSERIAL_STATS
struct i2cserial_stats__ {
  char firmware_version[8];
  long int serio_access;
  long int serio_err;
  int smbrq;
  int smb_err;
  int i2crq;
  int i2c_err;
} i2cserial_stats ;
# define STAT(x) {x;}
#else
# define STAT(x) {;}
#endif

#define I2CS_ERR(fmt, arg...)  printk(KERN_ERR  "i2c_serial {%s}: " fmt "\n" , __FUNCTION__ , ## arg)
#define I2CS_INFO(fmt, arg...)  printk(KERN_INFO  "i2c_serial [%s]: " fmt "\n" , __FUNCTION__ , ## arg)
#if I2CSERIAL_DEBUG==1
#define I2CS_DEBUG(fmt, arg...)  printk(KERN_DEBUG  "i2c_serial (%s): " fmt "\n" , __FUNCTION__ , ## arg)
#else
#define I2CS_DEBUG(fmt, arg...)  while (0) {}
#endif


/**
 * internal function to handle i2c buffer
 */
static inline char *serial_append_escaped_byte(unsigned char *p, unsigned char c)
{
    if (c == ESCAPE_CHAR)
      *p++ = ESCAPE_CHAR;
    *p++ = c;
    return p;
}

static unsigned char *serial_append_read(unsigned char *p, unsigned char address, unsigned char size)
{
  *p++ = ESCAPE_CHAR;
  *p++ = CMD_READ;
  *p++ = (unsigned char)(address & 0x7f);
  p = serial_append_escaped_byte(p, size);
  return p;
}

static unsigned char *serial_append_write(unsigned char *p, unsigned char address, unsigned char data)
{
  *p++ = ESCAPE_CHAR;
  *p++ = CMD_WRITE;
  *p++ = (unsigned char)(address & 0x7f);
  p = serial_append_escaped_byte(p, (unsigned char)(data & 0xff));
  return p;
}

static unsigned char *serial_append_write_block(unsigned char *p,
                                                unsigned char address,
                                                unsigned char *data,
                                                unsigned char size)
{
  int   i = 0;

  *p++ = ESCAPE_CHAR;
  *p++ = CMD_WRITE;
  *p++ = (unsigned char)(address & 0x7f);
  for(i = 0; i< size; ++i) {
    p = serial_append_escaped_byte(p, data[i]);
  }
  return p;
}

static unsigned char *serial_append_footer(unsigned char *p)
{
  *p++ = ESCAPE_CHAR;
  *p++ = CMD_EOT;
  return p;
}



/* {{{ i2c_serial_get_version : ask PSoC for firmware version
 * */
static int i2c_serial_get_version(struct i2c_adapter *adapter,char * version) {

  struct serio          *serio = adapter->algo_data;
  struct i2c_data       *i2cd = serio_get_drvdata(serio);
  unsigned int          i = 0;

  for (i=0; i<8; ++i) {
    i2cd->buffer[i] = 0;
    STAT(i2cserial_stats.firmware_version[i] = 0);
  }
  i2cd->irq_buffer = &(i2cd->buffer[0]);

  I2CS_DEBUG("Asking for PSoC firmware version...");
  I2CS_DEBUG("serio_write(0x%02x)", (unsigned)ESCAPE_CHAR);

  STAT(i2cserial_stats.serio_access++;)
  if (serio_write(serio, ESCAPE_CHAR) < 0) {
    I2CS_ERR("serio_write(0x%02x) failed", (unsigned)ESCAPE_CHAR);
    STAT(i2cserial_stats.serio_err++);
  }

  I2CS_DEBUG("serio_write(0x%02x)", (unsigned)CMD_VERSION);
  STAT(i2cserial_stats.serio_access++);
  if (serio_write(serio, CMD_VERSION) < 0) {
    I2CS_ERR("serio_write(0x%02x) failed", (unsigned)CMD_VERSION);
    STAT(i2cserial_stats.serio_err++);
  }

  i2cd->state = I2C_STATE_WAIT;
  wait_event_interruptible_timeout(wq, i2cd->state == I2C_STATE_IDLE, TIMEOUT);

  STAT(strncpy(i2cserial_stats.firmware_version, i2cd->buffer, 7));
  
  if (!strlen(i2cd->buffer))
  {
    return -1;
  }
  strncpy( i2cd->buffer,version,7);
  return 0;
} // i2c_serial_get_version }}}


//{{{ i2c_serio_i2c_xfer
static s32 i2c_serio_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg messages[], int num) {

  struct serio *serio ;
  struct i2c_data *i2cd ;
  unsigned int i=0;
  unsigned char *p, *q;
  s32 ret = 0;
  s32 tmp = 0;

  if (!adapter) {
    printk(KERN_ERR "i2c-serial (%s): NULL adapter !", __FUNCTION__);
    return -EINVAL;
  }

  serio = adapter->algo_data;
  i2cd = serio_get_drvdata(serio);
  I2CS_DEBUG("J:%ld - %d I2C msg", jiffies, num);
  STAT(i2cserial_stats.i2crq++);
  for (i=0; i<num; ++i) {

    if (messages[i].len > (I2C_SERIAL_BUFFER_LENGTH-1) ) {
      I2CS_ERR("Cannot handle requests more than %d bytes long (asked=%d) !", (I2C_SERIAL_BUFFER_LENGTH-1), messages[i].len);
      ret = -EMSGSIZE; // Message too long
      break;
    }

    if ( ! (i2cd->state & I2C_STATE_IDLE) )
      wait_event_interruptible_timeout(wq, (i2cd->state &I2C_STATE_IDLE) == I2C_STATE_IDLE, TIMEOUT);

    if ( (i2cd->state & I2C_STATE_IDLE) != I2C_STATE_IDLE ) {
      I2CS_ERR("PSoC jammed !"); /* TODO reset PSoC*/
      i2cd->state = I2C_STATE_IDLE;
    }

    i2cd->jiffies = jiffies;
    i2cd->error = 0;
    i2cd->reply_count = 1;

    p = i2cd->buffer;
    if (messages[i].flags & I2C_M_RD) {
      p = serial_append_read(p, messages[i].addr, messages[i].len);
      i2cd->state = I2C_STATE_READ;
      /*  prepare irq_buffer */
      i2cd->irq_buffer = messages[i].buf;
      i2cd->pos = 0;
    } else {
      p = serial_append_write_block(p, messages[i].addr, messages[i].buf, messages[i].len);
      p = serial_append_footer(p);
      i2cd->state = I2C_STATE_WAIT;
    }

    /* send command via serio */
    tmp=0;
    for (q=i2cd->buffer; q<p; q++) {
      STAT(i2cserial_stats.serio_access++);
      tmp+=ret=serio_write(serio, *q);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }
    }
    if ( tmp < 0 ) {
      I2CS_DEBUG("serio_write() errors (%d)", -tmp);
      break;
    }

    // wait for interrupt return
    wait_event_interruptible_timeout(wq, (i2cd->state & I2C_STATE_IDLE) == I2C_STATE_IDLE, TIMEOUT);

    if (! (i2cd->state & I2C_STATE_IDLE) ) {
//            I2CS_INFO("Interrupt timed out...");
#if I2CSERIAL_DEBUG==1
      char tmp_str[1024];
      int idx=0;
      idx=sprintf(&tmp_str[0],"{");
      for(q=i2cd->buffer; q<p; ++q) {
        idx+=sprintf(&tmp_str[idx],"%02x,", *q);
      }
      idx+=sprintf(&tmp_str[idx],"}");
      tmp_str[idx]=0;
      I2CS_DEBUG("J:%d+%u time out sending buffer[]=%s", i2cd->jiffies, (unsigned)(jiffies - i2cd->jiffies), tmp_str);
#endif
      i2cd->state = I2C_STATE_WAIT_WATCHDOG;
      //        I2CS_DEBUG("serio_write(0x%02x)", (unsigned)ESCAPE_CHAR);
#ifdef I2CSERIAL_STATS
      STAT(i2cserial_stats.serio_access++);
#endif
      ret=serio_write(serio, ESCAPE_CHAR);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }
      STAT(i2cserial_stats.serio_access++);
      ret=serio_write(serio, CMD_RESET);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }
      wait_event_interruptible_timeout(wq, i2cd->state == I2C_STATE_IDLE, TIMEOUT);
      ret=-ETIME; // "Timer expired"
      break;
    }

    I2CS_DEBUG("xfer complete in %u jif", (unsigned)(jiffies - i2cd->jiffies));

    // handle return values (errors and buffers)
    if (i2cd->error) {
#if I2CSERIAL_DEBUG==1
      char tmp_str[1024];
      int idx=0;
      idx=sprintf(&tmp_str[0],"{");
      for(q=i2cd->buffer; q<p; ++q) {
        idx+=sprintf(&tmp_str[idx],"%02x,", *q);
      }
      idx+=sprintf(&tmp_str[idx],"}");
      tmp_str[idx]=0;
      I2CS_DEBUG("error(%d) sending buffer[]=%s", i2cd->error, tmp_str);
#endif
      ret=i2cd->error;
      i2cd->error=0;
      break;
    }
    ret+=messages[i].len;
  }
  if (ret < 0) {
    STAT(i2cserial_stats.i2c_err++);
  }
  return ret;
} //i2c_serio_i2c_xfer }}}

/* {{{ i2c_serio_smbus_xfer : SMBus transfer callback */
static s32 i2c_serio_smbus_xfer(struct i2c_adapter *adapter,
                                u16 address,
                                unsigned short flags,
                                char rw,
                                u8 command,
                                int size,
                                union i2c_smbus_data *data)
{
  struct serio *serio = adapter->algo_data;
  struct i2c_data *i2cd = serio_get_drvdata(serio);
  unsigned char *p, *i;
  int ret=0;
  int tmp=0;

  I2CS_DEBUG("J:%lu @%x (rw=%d), cmd%02x, sz%d, dp%p", jiffies,
             (unsigned)address, (int)rw, (unsigned)command, (int)size, data);
  STAT(i2cserial_stats.smbrq++);

  if ( ! (i2cd->state & I2C_STATE_IDLE) )
    wait_event_interruptible_timeout(wq, (i2cd->state &I2C_STATE_IDLE) == I2C_STATE_IDLE, TIMEOUT);

  if ( ! (i2cd->state & I2C_STATE_IDLE) ) {
    I2CS_ERR("PSoC jammed..."); /* TODO : reset */
    i2cd->state = I2C_STATE_IDLE;
  }

  do {

    i2cd->pos = 0;
    p = i2cd->buffer;

    i2cd->reply_count = 1;

    switch (size) {

    case I2C_SMBUS_BYTE:
      if (rw == I2C_SMBUS_WRITE) {
        p = serial_append_write(p, address, command);
        p = serial_append_footer(p);
      } else {
        p = serial_append_read(p, address, 1);
      }
      break;

    case I2C_SMBUS_BYTE_DATA:
      if (rw == I2C_SMBUS_WRITE){
        p = serial_append_write(p, address, command);
        p = serial_append_escaped_byte(p, (unsigned char)(data->byte & 0xff));
        p = serial_append_footer(p);
      } else {
        p = serial_append_write(p, address, command);
        p = serial_append_footer(p);
        p = serial_append_read(p, address, 1);
        i2cd->reply_count = 2;
      }
      break;

      /**
       * DATA_ALL_SIZE : tweak (see i2c-dev.c) to pass arbitrary size
       * tweak not needed in future kernel
       * I think the transition with newer kernel will be smooth:
       * just remove I2C_SMBUS_I2C_BLOCK_DATA_ALL_SIZE
       * but verify what I2C_SMBUS_I2C_BLOCK_DATA and I2C_SMBUS_I2C_BLOCK_BROKEN do
       */
    case I2C_SMBUS_I2C_BLOCK_DATA:
//    case I2C_SMBUS_I2C_BLOCK_DATA_ALL_SIZE:
      if (rw == I2C_SMBUS_WRITE) {
        p = serial_append_write(p, address, command);
        for(i = data->block + 1; i <= data->block + data->block[0]; i++)
          p = serial_append_escaped_byte(p, *i);
        p = serial_append_footer(p);
      } else {
        I2CS_DEBUG("read_block_data: %d", data->block[0]);
        p = serial_append_write(p, address, command);
        p = serial_append_footer(p);
        //we will receive data block + 1 (the size)
        p = serial_append_read(p, address, data->block[0] + 1);
        i2cd->reply_count = 2;
      }
      break;

    case I2C_SMBUS_BLOCK_DATA:
      if (rw == I2C_SMBUS_WRITE) {
        p = serial_append_write(p, address, command);
        for(i = data->block; i <= data->block + data->block[0]; i++)
          p = serial_append_escaped_byte(p, *i);
        p = serial_append_footer(p);
      } else {
        /**
         * we can't handle this case : because the size is not specified
         * the psoc need to read the first byte (the size) from the slave
         * then he can nack when all data is received
         *
         * psoc doesnt handle this case
         */
        I2CS_ERR("Unsupported transaction size %d", size);
      }
      break;

    default:
      I2CS_ERR("Unsupported transaction size %d", size);
      ret=-ENOTSUPP;
      break;
    }

    //i2cd->jiffies = jiffies;
    if (rw == I2C_SMBUS_WRITE) {
      i2cd->state = I2C_STATE_WAIT;
    } else {
      i2cd->state = I2C_STATE_READ;
      /*  prepare irq_buffer */
      i2cd->irq_buffer = &(i2cd->buffer[0]);
    }

    I2CS_DEBUG("send %dB seriobuf", p-i2cd->buffer);

    /* send command via serio */
    tmp=0;
    for (i = i2cd->buffer; i < p; i++) {
      STAT(i2cserial_stats.serio_access++);
      tmp+=ret=serio_write(serio, *i);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }
    }
    if ( tmp < 0 ) {
      I2CS_DEBUG("serio_write() errors (%d)", -tmp);
    }

    /* command is always sent in 0 jiffies ! (buffered)
       I2CS_DEBUG("command sent in %u jiffies", (unsigned)(jiffies - i2cd->jiffies));
    */
    i2cd->jiffies = jiffies;
    i2cd->error = 0;
    i2cd->pos = 0;

    wait_event_interruptible_timeout(wq, (i2cd->state & I2C_STATE_IDLE) == I2C_STATE_IDLE, TIMEOUT);

    if (! (i2cd->state & I2C_STATE_IDLE) ) {
#if I2CSERIAL_DEBUG==1
      char tmp_str[256];
      int idx=0;
      idx=sprintf(&tmp_str[0],"{");
      for(i=i2cd->buffer; i<p; ++i) {
        idx+=sprintf(&tmp_str[idx],"%02x,", *i);
      }
      idx+=sprintf(&tmp_str[idx],"}");
      tmp_str[idx]=0;
      I2CS_DEBUG("J:%d+%u time out sending buffer[]=%s", i2cd->jiffies, (unsigned)(jiffies - i2cd->jiffies), tmp_str);
#endif
      I2CS_DEBUG("serio_write time out (err:%d), sending reset request...", i2cd->error);
      i2cd->state = I2C_STATE_WAIT_WATCHDOG;

      STAT(i2cserial_stats.serio_access++);
      ret=serio_write(serio, ESCAPE_CHAR);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }

      STAT(i2cserial_stats.serio_access++);
      ret=serio_write(serio, CMD_RESET);
      if (ret < 0) {
        STAT(i2cserial_stats.serio_err++);
      }
      wait_event_interruptible_timeout(wq, i2cd->state == I2C_STATE_IDLE, TIMEOUT);
      ret= -EIO;
      break;
    }

    /* TODO : extended error handling */
    if (i2cd->error) {
#if I2CSERIAL_DEBUG==1
      char tmp_str[256];
      int idx=0;
      idx=sprintf(&tmp_str[0],"{");
      for(i=i2cd->buffer; i<p; ++i) {
        idx+=sprintf(&tmp_str[idx],"%02x,", *i);
      }
      idx+=sprintf(&tmp_str[idx],"}");
      tmp_str[idx]=0;
      I2CS_DEBUG("error(%d) sending buffer[]=%s", i2cd->error, tmp_str);
#endif
      ret=i2cd->error;
      i2cd->error=0;
      break;
    }

    if (rw == I2C_SMBUS_READ) {
      if (size == I2C_SMBUS_I2C_BLOCK_DATA_ALL_SIZE)
      {
        int j;
        int sz = data->block[0];

        if (sz > i2cd->pos)
          sz = i2cd->pos;
        if (sz > 32)
          sz = 32;
        I2CS_DEBUG("read_block_data return: %d", sz);
        for(j = 0; j < sz; ++j)
          data->block[j+1] = i2cd->buffer[j];
      }
      else
        data->byte = i2cd->buffer[0];

    }

    ret=0;

  } while (0) ;

  I2CS_DEBUG("xfer complete in %u jif", (unsigned)(jiffies - i2cd->jiffies));
  if ( ret < 0 ) {
    STAT(i2cserial_stats.smb_err++);
  }
  return ret;
} //i2c_serio_smbus_xfer }}}

/* {{{ i2c_serio_func ireturn supported I2C/SMBus functionnalities */
static u32 i2c_serio_func(struct i2c_adapter *adapter)
{
  /*
    return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
    I2C_FUNC_SMBUS_I2C_BLOCK;
  */
  return I2C_FUNC_SMBUS_BYTE |
    I2C_FUNC_SMBUS_BYTE_DATA |
    I2C_FUNC_SMBUS_BLOCK_DATA |
    I2C_FUNC_SMBUS_I2C_BLOCK;
} // i2c_serio_func }}}

static const struct i2c_algorithm i2c_algo = {
  .master_xfer          = i2c_serio_i2c_xfer,
  .smbus_xfer           = i2c_serio_smbus_xfer,
  .functionality	= i2c_serio_func,
};

/* {{{ i2c_interrupt : serial interrupt service routine
   - store serial data into irqbuffer
*/
static irqreturn_t i2c_interrupt(struct serio *serio, unsigned char data,
                                 unsigned int flags)
{
  struct i2c_data *i2cd = NULL;

  i2cd = serio_get_drvdata(serio);

  I2CS_DEBUG("%02x[%x],%d+%u", (unsigned)data, flags, i2cd->jiffies, (unsigned)(jiffies - i2cd->jiffies));

  switch (i2cd->state) {
  case I2C_STATE_WAIT:
    /* we're waiting for reply (but no data), we must read [Esc]. Ignore
     * anything else*/
    if (data == ESCAPE_CHAR) {
      i2cd->state = I2C_STATE_CMD;
      i2cd->error = 0;
      //I2CS_DEBUG("Command ACK in %d jiffies", (int)(jiffies - i2cd->jiffies));
    }
    break;

  case I2C_STATE_CMD:
    switch (data) {
    case ESCAPE_CHAR: //escaped ESC caracter
      if (i2cd->pos >= I2C_SERIAL_BUFFER_LENGTH) {
        I2CS_ERR("Buffer overflow ! (0x%02x)", data);
      } else {
        i2cd->irq_buffer[i2cd->pos++] = data;
        //i2cd->state=I2C_STATE_READ;
      }
      break;
    case REPLY_NAK:
      i2cd->error=I2C_ERROR_ADDRNAK;
      I2CS_DEBUG("Slave not existing (0x%02x).", data);
      i2cd->reply_count--;
      break;
    case REPLY_ADDRNAK:
      i2cd->error=I2C_ERROR_NAK;
      I2CS_DEBUG("Slave not responding (0x%02x).", data);
      i2cd->reply_count--;
      break;
    case REPLY_OVERFLOW:
      i2cd->error=I2C_ERROR_OVERBUFF;
      I2CS_DEBUG("PSoC buffer overflow.");
      i2cd->reply_count--;
      break;
    case 'F':
      i2cd->error=I2C_ERROR_MTF;
      I2CS_DEBUG("Master transmit failure (Wow, how can it happen ?).");
      i2cd->reply_count--;
      break;
    case REPLY_STALLED:
      i2cd->error=I2C_ERROR_STALL;
      I2CS_DEBUG("I2C bus stalled.");
      i2cd->reply_count--;
      break;
    case REPLY_TIMEOUT:
      i2cd->error=I2C_ERROR_TIMEOUT;
      I2CS_DEBUG("No activity within timeout period.");
      i2cd->reply_count--;
      break;
    case REPLY_CANCELREAD:
    case REPLY_CANCELWRITE:
      i2cd->error=I2C_ERROR_BUSY;
      I2CS_DEBUG("Device busy (or garbled), command refused.");
      i2cd->reply_count--;
      break;
    case CMD_RESET:
      i2cd->error=I2C_ERROR_RESET;
      I2CS_DEBUG("Device reset (watchdog timed out).");
      i2cd->reply_count=0;
      break;
    case CMD_VERSION:
      i2cd->reply_count=4;
      i2cd->pos=0;
      i2cd->state=I2C_STATE_VERSION;
      break;

    case CMD_WAKEUP:
      I2CS_DEBUG("Received wakeup command from PSoC.");
      break;

    default:
      if (data>='A' && data <='Z') {
        /* write subcommand completed successfully */
        i2cd->reply_count--;
        break;
      }
      if (data>='a' && data <='z') {
        /* read subcommand completed successfully */
        i2cd->reply_count--;
        break;
      }
      I2CS_INFO("Ignoring unexpected 'ESC-Ox%02x' from PSoC.", data);
    }
    if (i2cd->reply_count==0) {
      /* command completed , PSoC will reset soon if no more commands
       * we must have another wait state, which will not wake_up*/
      i2cd->state = I2C_STATE_WAIT_WATCHDOG;
      wake_up_interruptible(&wq);
    } else {
      if (i2cd->state != I2C_STATE_VERSION) {
        i2cd->state = I2C_STATE_READ;
      }
    }
    break;

  case I2C_STATE_VERSION:
    i2cd->irq_buffer[i2cd->pos++] = data;
    if (--i2cd->reply_count  == 0) {
      i2cd->state = I2C_STATE_WAIT_WATCHDOG;
      wake_up_interruptible(&wq);
    }
    break;

  case I2C_STATE_WAIT_WATCHDOG:
    if (data==ESCAPE_CHAR) {
      //ignore it; if it is a reply, we should be in WAIT state
    }
    if (data==CMD_RESET) {
      // PSoC WDR complete, nothing more should come
      i2cd->state = I2C_STATE_IDLE;
    }
    break;

  case I2C_STATE_READ:
    if (data==ESCAPE_CHAR) {
      i2cd->state = I2C_STATE_CMD;
    } else {
      if (i2cd->pos >= I2C_SERIAL_BUFFER_LENGTH) {
        I2CS_ERR("Buffer overflow !! (0x%02x)", data);
      } else {
        i2cd->irq_buffer[i2cd->pos++] = data;
      }
    }
    break;

  default:
    /* invalid I2C_STATE */
    I2CS_DEBUG("Spurious int. data %02x state %d",
               (unsigned)data, i2cd->state);
  }
  return IRQ_HANDLED;
} // i2c_interrupt }}}

/* {{{ i2c_disconnect : serial port disconnection callback */
static void i2c_disconnect(struct serio *serio)
{
  struct i2c_data *i2cd = serio_get_drvdata(serio);

  i2c_del_adapter(&i2cd->adapter);
  serio_close(serio);
  serio_set_drvdata(serio, NULL);
  kfree(i2cd);
} // i2c_disconnect }}}

/* {{{ i2c_connect : line discipline registration
   + I2C adapter registration
*/
static int i2c_connect(struct serio *serio, struct serio_driver *drv)
{
  struct i2c_data *i2cd;
  struct i2c_adapter *adapter;
  char version[42];
  int err;

  I2CS_DEBUG("i2c_connect");

  i2cd = kzalloc(sizeof(struct i2c_data), GFP_KERNEL);
  if (!i2cd) {
    err = -ENOMEM;
    goto exit;
  }
  i2cd->state = I2C_STATE_IDLE;

  serio_set_drvdata(serio, i2cd);

  I2CS_DEBUG("Opening serio with driver '%s'.", drv->description);

  err = serio_open(serio, drv);
  if (err) {
    I2CS_ERR("serio_open failed %d", err);
    goto exit_kfree;
  }

  adapter = &i2cd->adapter;
  adapter->owner = THIS_MODULE;
  adapter->algo = &i2c_algo;
  adapter->algo_data = serio;
  adapter->dev.parent = &serio->dev;
  adapter->id = I2C_HW_B_SER;

  strlcpy(adapter->name, DRV_NAME, strlen(DRV_NAME));

  err = i2c_serial_get_version(adapter,version);
  
  if (err) {
    I2CS_ERR("PSoC not found");
    goto exit_close;
  }
  
  I2CS_INFO("PSoC firmware version '%s'.", version);
  
  err = i2c_add_adapter(adapter);
  
  if (err) {
    I2CS_ERR("adapter registration failed.");
    goto exit_close;
  }

  I2CS_INFO("I2C over serial port registered.");


  return 0;

 exit_close:
  serio_close(serio);
 exit_kfree:
  serio_set_drvdata(serio, NULL);
  kfree(i2cd);
 exit:
  return err;
} // i2c_connect }}}

/* descripteur du line discipline */
static struct serio_device_id i2c_serio_ids[] = {
  {
    .type       = SERIO_RS232,
    .proto      = 0x77,
    .id         = SERIO_ANY,
    .extra      = SERIO_ANY,
  },
  { 0 }
};
MODULE_DEVICE_TABLE(serio, i2c_serio_ids);

/* descripteur du driver serio */
static struct serio_driver i2c_drv = {
  .driver = {
    .name = "i2c_serio",
  },
  .description = "I2C on serial port PSoC (Aldebaran)",
  .id_table = i2c_serio_ids,
  .connect = i2c_connect,
  .disconnect = i2c_disconnect,
  .interrupt = i2c_interrupt,
};

#ifdef I2CSERIAL_STATS
/* {{{ proc_i2cstats_read : report debugging statistical info to /proc
 * TODO check len is always<count !!!! */
int proc_i2cstats_read(char* buf, char** start, off_t offset,
                       int count, int* eof, void* data) {
  int len=0;
  len += sprintf(buf+len,"I2C-serial driver version '%s'\n", VERSION);
  len += sprintf(buf+len,"PSoC firmware version '%s'\n", i2cserial_stats.firmware_version);
  len += sprintf(buf+len,"I2C_xfr: %8d, %d error(s)\n", i2cserial_stats.i2crq, i2cserial_stats.i2c_err);
  len += sprintf(buf+len,"SMBus  : %8d, %d error(s)\n", i2cserial_stats.smbrq, i2cserial_stats.smb_err);
  len += sprintf(buf+len,"Serial : %8ld B, %ld error(s)\n", i2cserial_stats.serio_access, i2cserial_stats.serio_err);
  0[eof]=1;
  return len;
} // }}}
#endif

/* initialisation du module et enregistrement du driver serio */
static int __init i2c_serio_init(void)
{
  int ret=0;
  I2CS_INFO("I2C serio driver ver %s", VERSION);
  ret = serio_register_driver(&i2c_drv);
#ifdef I2CSERIAL_STATS
  for (ret=0; ret<sizeof(struct i2cserial_stats__); ++ret) {
    *((char*)(&i2cserial_stats) + ret) = '\0';
  }
  create_proc_read_entry("i2cstats", 0, NULL, proc_i2cstats_read, NULL);
#endif
  return 0;
}

static void __exit i2c_serio_exit(void)
{
  STAT(remove_proc_entry("i2cstats", NULL));
  serio_unregister_driver(&i2c_drv);
}

module_init(i2c_serio_init);
module_exit(i2c_serio_exit);
