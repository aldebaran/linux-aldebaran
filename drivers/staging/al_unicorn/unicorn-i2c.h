#ifndef UNICORNI2C
#define UNICORNI2C


#define I2C_CONTROL_BUS_CLEAR_POS               0
#define I2C_CONTROL_BUS_CLEAR_MASK              (1<<I2C_CONTROL_BUS_CLEAR_POS)
#define I2C_BUS_CLEAR_CLEAR                     I2C_CONTROL_BUS_CLEAR_MASK

#define I2C_CONTROL_ADDR_MODE_POS               2
#define I2C_CONTROL_ADDR_MODE_MASK		          (1<<I2C_CONTROL_ADDR_MODE_POS)
#define I2C_ADDR_MODE_16_BITS 			            0
#define I2C_ADDR_MODE_8_BITS 			              I2C_CONTROL_ADDR_MODE_MASK

#define I2C_CONTROL_DIRECT_MODE_POS		          3
#define I2C_CONTROL_DIRECT_MODE_MASK	          (1<<I2C_CONTROL_DIRECT_MODE_POS)
#define I2C_DIRECT_MODE_NORMAL 			            0
#define I2C_DIRECT_MODE_DIRECT 			            I2C_CONTROL_DIRECT_MODE_MASK

#define I2C_CONTROL_RNW_POS				              4
#define I2C_CONTROL_RNW_MASK			              (1<<I2C_CONTROL_RNW_POS)
#define I2C_RNW_WRITE					                  0
#define I2C_RNW_READ 					                  I2C_CONTROL_RNW_MASK

#define I2C_CONTROL_START_POS			              5
#define I2C_CONTROL_START_MASK			            (1<<I2C_CONTROL_START_POS)
#define I2C_START 						                  I2C_CONTROL_START_MASK

#define I2C_CONTROL_FREQ_SEL_POS		            16
#define I2C_CONTROL_FREQ_SEL_MASK		            (1<<I2C_CONTROL_FREQ_SEL_POS)
#define I2C_BUS_SPEED_100KBITS 			            0
#define I2C_BUS_SPEED_400KBITS			            I2C_CONTROL_FREQ_SEL_MASK

#define I2C_CONTROL_SLAVE_SEL_POS		            17
#define I2C_CONTROL_SLAVE_SEL_MASK		          (7<<I2C_CONTROL_SLAVE_SEL_POS)

#define I2C_CONTROL_RD_FIFO_CLEAR_POS			      20
#define I2C_CONTROL_RD_FIFO_CLEAR_MASK			    (1<<I2C_CONTROL_RD_FIFO_CLEAR_POS)
#define I2C_RD_FIFO_CLEAR                       I2C_CONTROL_RD_FIFO_CLEAR_MASK

#define I2C_CONTROL_WR_FIFO_CLEAR_POS			      21
#define I2C_CONTROL_WR_FIFO_CLEAR_MASK			    (1<<I2C_CONTROL_WR_FIFO_CLEAR_POS)
#define I2C_WR_FIFO_CLEAR                       I2C_CONTROL_WR_FIFO_CLEAR_MASK









int i2c_unicorn_register(struct unicorn_dev * dev, struct i2c_adapter * adapter);
void i2c_unicorn_unregister(struct i2c_adapter * adapter);
int unicorn_init_i2c(struct unicorn_dev * dev);
void unicorn_i2c_remove(struct unicorn_dev * dev);
#endif
