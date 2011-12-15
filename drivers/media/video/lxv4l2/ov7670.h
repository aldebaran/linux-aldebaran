#ifndef __OV7670_H__
#define __OV7670_H__
#include <linux/i2c.h>

extern int ov7670_mod_init(void);
extern int ov7670_mod_exit(void);

extern struct i2c_client *ov7670_i2c_client;

extern int ov7670_command(struct i2c_client *, unsigned int, void *);
#endif
