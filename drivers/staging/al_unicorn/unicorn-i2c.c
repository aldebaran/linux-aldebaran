/*
  unicorn-i2c.c - V4L2 driver for unicorn

  Copyright (c) 2010 Aldebaran robotics
  joseph pinkasfeld joseph.pinkasfeld@gmail.com
  Ludovic SMAL <lsmal@aldebaran-robotics.com>

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

#define DEBUG 1

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>


#include "unicorn.h"
#include "unicorn-i2c.h"


volatile static struct abh32_i2c_master * fpgaAddress;
static unsigned long functionality = ~0UL;
struct mutex * i2c_mutex;
volatile static int * i2c_error;
//volatile static int * i2c_eof;

static void set_control(void)
{
  fpgaAddress->control = I2C_ADDR_MODE_8_BITS | I2C_BUS_SPEED_100KBITS | I2C_DIRECT_MODE_DIRECT;
}

static int unicorn_restart_i2c(void)
{
  int pass=0;

  printk(KERN_DEBUG "unicorn i2c restart");
  fpgaAddress->address = 0;
  fpgaAddress->burst_size = 0;

  fpgaAddress->control |= I2C_RD_FIFO_CLEAR | I2C_WR_FIFO_CLEAR | I2C_BUS_CLEAR_CLEAR | I2C_START;


  msleep(1);
  while(fpgaAddress->status.busy){
  if (pass++>100000)
  {
    printk(KERN_DEBUG "unicorn_restart_i2c:Error unicorn i2c too long to answer\n");
    return -1;
  }
  }

  fpgaAddress->control &= ~I2C_BUS_CLEAR_CLEAR;
  fpgaAddress->control &= ~I2C_CONTROL_SLAVE_SEL_MASK;


  set_control();
  if((fpgaAddress->control & I2C_CONTROL_START_MASK) == I2C_CONTROL_START_MASK)
  {
    printk(KERN_ERR "unicorn_restart_i2c:ERROR unicorn i2c start bit not idle\n");
    return -1;
  }

  return 0;
}

static int unicorn_wait_busy(void)
{
  int pass=0;
  while(fpgaAddress->status.busy){

    if (pass++>10000)
    {
      printk(KERN_DEBUG "unicorn_wait_busy:Error unicorn i2c too long to answer\n");
      unicorn_restart_i2c();
      return -1;
    }
  }
  return 0;
}


static int unicorn_write_comand_n_byte(struct i2c_adapter * adap,u8 command, u8 size, u8* data)
{
  int i=0;
  int totalwritten=0;

  *i2c_error = 0;

  fpgaAddress->control = I2C_ADDR_MODE_8_BITS | I2C_BUS_SPEED_100KBITS | I2C_DIRECT_MODE_DIRECT
                         | I2C_RNW_WRITE
                         | I2C_WR_FIFO_CLEAR
                         | (adap->nr << I2C_CONTROL_SLAVE_SEL_POS)
                         ;


  fpgaAddress->subaddress = 0x0;




  if (!fpgaAddress->status.wr_fifo_empty )
  {
    printk(KERN_ERR "unicorn_write_comand_n_byte:unicorn i2c try to write but fifo is not empty\n");
    fpgaAddress->control |= I2C_WR_FIFO_CLEAR;
    if (!fpgaAddress->status.wr_fifo_empty )
    {
      printk(KERN_ERR "unicorn_write_comand_n_byte:unicorn i2c fifo still not empty\n");
    }
  }
  if ( !fpgaAddress->status.wr_fifo_full )
  {
    fpgaAddress->wdata = command;
    totalwritten++;
  }
  else
  {
    printk(KERN_ERR "unicorn_write_comand_n_byte:unicorn i2c try to write but fifo full\n");
    return -1;
  }

  for(i=0; i<size; i++)
  {
    if ( !fpgaAddress->status.wr_fifo_full )
    {
      fpgaAddress->wdata = data[i];
      totalwritten++;
    }
    else
    {
      printk(KERN_ERR "unicorn_write_comand_n_byte:unicorn i2c try to write but fifo full\n");
      return -1;
    }
  }
  fpgaAddress->burst_size = totalwritten - 1;

  fpgaAddress->control |= I2C_START;


  msleep(size/10 +1);
  if(unicorn_wait_busy())
  {
    return -1;
  }
  if(*i2c_error)
  {
    *i2c_error = 0;
    return -EREMOTEIO;
  }
  return 0;
}

static int unicorn_read_n_byte(struct i2c_adapter * adap,u8 size,u8 * buf)
{
  int read_index = 0;
  *i2c_error = 0;

  fpgaAddress->control |= I2C_RD_FIFO_CLEAR;

  if(!fpgaAddress->status.rd_fifo_empty)
  {
    printk(KERN_ERR "unicorn_read_n_byte:unicorn i2c error RX fifo not empty : clearing\n");
    fpgaAddress->control |= I2C_RD_FIFO_CLEAR;

    if(!fpgaAddress->status.rd_fifo_empty)
    {
      printk(KERN_ERR "unicorn_read_n_byte:unicorn i2c error RX fifo still not empty\n");
    }
  }

  fpgaAddress->burst_size = size-1;

  fpgaAddress->control = I2C_ADDR_MODE_8_BITS | I2C_BUS_SPEED_100KBITS | I2C_DIRECT_MODE_DIRECT
                         | I2C_RNW_READ
                         | (adap->nr << I2C_CONTROL_SLAVE_SEL_POS)
                         | I2C_START;





  msleep(size/10 +1);
  if(unicorn_wait_busy())
  {
    return -1;
  }
  while(!fpgaAddress->status.rd_fifo_empty)
  {
    buf[read_index++] = fpgaAddress->rdata;
  }
  if(*i2c_error)
  {
    *i2c_error = 0;
    return -EREMOTEIO;
  }
  return 0;
}

/* Return negative errno on error. */
static s32 unicorn_smbus_xfer(struct i2c_adapter * adap,
                              u16 addr, unsigned short flags,
                              char read_write,
                              u8 command,
                              int size,
                              union i2c_smbus_data * data)
{
  s32 ret=0;
  int datasize;

  mutex_lock(i2c_mutex);

  /* set i2c slave address*/
  fpgaAddress->address = addr;


  switch (size) {

  case I2C_SMBUS_QUICK:
    dprintk(1, dev_name(&adap->dev), "smbus quick - addr 0x%02x unimplemented\n", addr);
    ret = -EOPNOTSUPP;
    break;

  case I2C_SMBUS_BYTE:
    if (read_write == I2C_SMBUS_WRITE) {
      ret = unicorn_write_comand_n_byte(adap,command,0,data->block);
      dprintk(3, dev_name(&adap->dev), "smbus write byte - addr 0x%02x, "
              "wrote 0x%02x at 0x%02x.\n",
              addr, data->byte, command);
    } else {
      ret = unicorn_read_n_byte(adap,1,data->block);
      dprintk(3, dev_name(&adap->dev), "smbus read byte - addr 0x%02x, "
                    "wrote 0x%02x at 0x%02x.\n",
                    addr, data->byte, command);
    }
    break;

  case I2C_SMBUS_BYTE_DATA:
    if (read_write == I2C_SMBUS_WRITE) {
      ret = unicorn_write_comand_n_byte(adap,command,1,data->block);
      dprintk(3, dev_name(&adap->dev), "smbus write byte data - addr 0x%02x, "
              "wrote 0x%02x at 0x%02x.\n",
              addr, data->byte, command);
    } else {
      ret = unicorn_write_comand_n_byte(adap,command,0,data->block);
      if (ret)
      {
        break;
      }
      ret = unicorn_read_n_byte(adap,1,data->block);
      if (ret)
      {
        dprintk(3, dev_name(&adap->dev), "smbus read byte data - addr 0x%02x, "
            "read  0x%02x at 0x%02x. (%x)\n",
            addr, data->byte, command, ret);
      }
    }
    break;

  case I2C_SMBUS_WORD_DATA:
    if (read_write == I2C_SMBUS_WRITE) {
      //chip->words[command] = data->word;
      dprintk(3, dev_name(&adap->dev), "write smbus word data - addr 0x%02x, "
              "wrote 0x%04x at 0x%02x. unimplemented\n",
              addr, data->word, command);
      ret = -EOPNOTSUPP;
    } else {
      //data->word = chip->words[command];
      dprintk(3, dev_name(&adap->dev), "read smbus word data - addr 0x%02x, "
              "read  0x%04x at 0x%02x. unimplemented\n",
              addr, data->word, command);
      ret = -EOPNOTSUPP;
    }
    break;


  case I2C_SMBUS_I2C_BLOCK_DATA:
    datasize = data->block[0];
    if (read_write == I2C_SMBUS_WRITE) {

      ret = unicorn_write_comand_n_byte(adap,command,datasize,data->block+1);
      dprintk(3, dev_name(&adap->dev), "i2c smbus i2c write block data - addr 0x%02x, "
              "wrote %d bytes at 0x%02x.\n",
              addr, datasize, command);
    } else {
      ret = unicorn_write_comand_n_byte(adap,command,0,NULL);
      if (ret)
      {
        break;
      }
      ret = unicorn_read_n_byte(adap,datasize, data->block+1);
      dprintk(3, dev_name(&adap->dev), "i2c smbus i2c read block data - addr 0x%02x, "
              "read  %d bytes at 0x%02x.\n",
              addr, datasize, command);
    }
    break;

  case I2C_SMBUS_BLOCK_DATA:
    if (read_write == I2C_SMBUS_WRITE) {
      ret = unicorn_write_comand_n_byte(adap,command,data->block[0]+1,data->block);
      dprintk(3, dev_name(&adap->dev), "smbus write block data - addr 0x%02x, "
              "wrote %d bytes at 0x%02x.\n",
              addr, data->block[0]+1, command);
      ret = 0;
    } else {

      dprintk(1, dev_name(&adap->dev), "i2c_smbus_read_block_data - addr 0x%02x unimplemented\n", addr);
      ret = -EOPNOTSUPP;
    }
    break;

  default:
    dprintk(1, dev_name(&adap->dev), "Unsupported I2C/SMBus command\n");
    ret = -EOPNOTSUPP;
    break;
  } /* switch (size) */
  mutex_unlock(i2c_mutex);
  return ret;
}


static int unicorn_i2c_xfer(struct i2c_adapter *i2c_adap,
             struct i2c_msg *msgs,
             int num)
{
  struct i2c_msg * msg=&msgs[0];
  int ret = 0;
  int im;
  int len=0;
  mutex_lock(i2c_mutex);

  for (im = 0; ret == 0 && im != num; im++) {
    msg = &msgs[im];

    fpgaAddress->address = msg->addr;
    if ((msg->flags&I2C_M_RD)) {
      if (msg->len == 0)
      {
        msg->len = 1;
        printk(KERN_ERR "unicorn_i2c_xfer:unicorn cannot receive 0 byte -> HACK \n");
      }
    /*  dev_dbg(&i2c_adap->dev, "read i2c byte - addr 0x%02x, "
                    "len=%d ret=%d im=%d\n",
                    msg->addr, msg->len, ret, im);*/
      ret = unicorn_read_n_byte(i2c_adap, msg->len, msg->buf);
    }
    else {
      if (msg->len == 0)
      {
        msg->len = 1;
        msg->buf[0] = 0;
        printk(KERN_ERR "unicorn_i2c_xfer:unicorn cannot transmit 0 byte -> HACK \n");
      }
      ret = unicorn_write_comand_n_byte(i2c_adap, msg->buf[0], msg->len - 1, &msg->buf[1]);
      /*dev_dbg(&i2c_adap->dev, "write i2c byte - addr 0x%02x, "
                    "len=%d ret=%d im=%d\n",
                    msg->addr, msg->len, ret, im);*/

    }

    if(!ret)
    {
      len+=msg->len;
    }
  }
  mutex_unlock(i2c_mutex);

  // return how many bytes was read or write
  if(ret==0)
  {
    ret= len;
  }

  return ret;
}


static u32 unicorn_func(struct i2c_adapter *adapter)
{
  return (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
          I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
          I2C_FUNC_SMBUS_BLOCK_DATA |I2C_FUNC_SMBUS_I2C_BLOCK |
          I2C_FUNC_I2C) & functionality;
}

const struct i2c_algorithm unicorn_i2c_algorithm = {
  .functionality	= unicorn_func,
  .master_xfer = unicorn_i2c_xfer,
  .smbus_xfer	= unicorn_smbus_xfer,
};

int i2c_unicorn_register(struct unicorn_dev * dev, struct i2c_adapter * adapter)
{
  int ret;

  i2c_set_adapdata(adapter, &dev->v4l2_dev);
  ret = i2c_add_numbered_adapter(adapter);

  printk(KERN_INFO "i2c successfully registered\n");
  return ret;
}

static struct i2c_adapter unicorn_i2c_adapters[] ={
{
  .owner		= THIS_MODULE,
  .class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
  .algo		= &unicorn_i2c_algorithm,
  .name		= "I2C SMBUS unicorn driver 0",
  .nr     = 0,
},
{
  .owner		= THIS_MODULE,
  .class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
  .algo		= &unicorn_i2c_algorithm,
  .name		= "I2C SMBUS unicorn driver 1",
  .nr     = 1,
},
{
  .owner		= THIS_MODULE,
  .class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
  .algo		= &unicorn_i2c_algorithm,
  .name		= "I2C SMBUS unicorn driver 2",
  .nr     = 2,
},
{
  .owner		= THIS_MODULE,
  .class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
  .algo		= &unicorn_i2c_algorithm,
  .name		= "I2C SMBUS unicorn driver 3",
  .nr     = 3,
},
{
  .owner                = THIS_MODULE,
  .class                = I2C_CLASS_HWMON | I2C_CLASS_SPD,
  .algo         = &unicorn_i2c_algorithm,
  .name         = "I2C SMBUS unicorn driver multicast",
  .nr     = 4,
}
};

int unicorn_init_i2c(struct unicorn_dev * dev)
{
  int ret = 0;
  int i;
  fpgaAddress = dev->i2c_master;
  i2c_mutex = &dev->i2c_mutex;
  i2c_error = &dev->i2c_error;

  ret = unicorn_restart_i2c();
  dev->interrupts_controller->irq.ctrl |=
      IT_I2C_ACCES_ERROR            |
      IT_I2C_WRITE_FIFO_ERROR       |
      IT_I2C_READ_FIFO_ERROR        |
      IT_RT;

  for(i=0;i<MAX_I2C_ADAPTER;i++)
  {
    if(i2c_unicorn_register(dev,&unicorn_i2c_adapters[i]))
    {
      printk(KERN_INFO "unicorn : fail to add i2c adapter \n");
      return -1;
    }
    dev->i2c_adapter[i] = &unicorn_i2c_adapters[i];
  }
  return ret;
}


void unicorn_i2c_unregister(struct i2c_adapter * adapter)
{
  i2c_del_adapter(adapter);
}

void unicorn_i2c_remove(struct unicorn_dev * dev)
{
  int i;

  for(i=0;i<MAX_I2C_ADAPTER;i++)
  {
#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
	int j;
	for (j=0; j<max_subdev_per_video_bus; j++)
	{
		if(dev->sensor[i][j]!=NULL)
		    v4l2_device_unregister_subdev(dev->sensor[i][j]);
	}
#endif
    unicorn_i2c_unregister(dev->i2c_adapter[i]);
  }
}
