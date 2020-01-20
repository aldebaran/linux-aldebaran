/*
 * TI serializer deserializer (DS90UB941 DS90UB948) pair driver
 *
 */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>


#define REG_SLAVE_ID_0		0x07
#define REG_SLAVE_ID_1		0x70
#define REG_SLAVE_ID_2		0x71
#define REG_SLAVE_ID_3		0x72

#define REG_SLAVE_ALIAS_0	0x08
#define REG_SLAVE_ALIAS_1	0x77
#define REG_SLAVE_ALIAS_2	0x78
#define REG_SLAVE_ALIAS_3	0x79


#define REMOTE_SLAVE_ADD_1	0x50	//NFC
#define REMOTE_SLAVE_ADD_2	0x78	//Backlight
#define REMOTE_SLAVE_ADD_3	0xBA	//TOUCH: changed to BA since slave address is changed to 0x5d from 0x14
#define REMOTE_SLAVE_ADD_4	0xA0	//EEPROM

#define SER_ADDRESS	0x0C
#define DES_ADDRESS	0x2C

struct serdes_941_948_data {
    struct i2c_client *client;
    bool serdes; /* 0 for serializer 1 for deserializer*/
};

static int serdes_941_init(struct serdes_941_948_data *data)
{
    //ds90ub941  Device initialization
    pr_info("%s\n",__func__);

    //Remote slave devices
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_0,    REMOTE_SLAVE_ADD_1);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_0, REMOTE_SLAVE_ADD_1);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_1,    REMOTE_SLAVE_ADD_2);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_1, REMOTE_SLAVE_ADD_2);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_2,    REMOTE_SLAVE_ADD_3);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_2, REMOTE_SLAVE_ADD_3);


    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_3, REMOTE_SLAVE_ADD_4);
    i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_3, REMOTE_SLAVE_ADD_4);

    //GENERAL_CFG
    i2c_smbus_write_byte_data(data->client, 0x03, 0x9A);

    //Disable DSI
    //i2c_smbus_write_byte_data(data->client, 0x01, 0x08);

    //Disable I2S
    i2c_smbus_write_byte_data(data->client, 0x54, 0x00);

    //GPIO config
    i2c_smbus_write_byte_data(data->client, 0x0F, 0x03);// GPIO 3
    i2c_smbus_write_byte_data(data->client, 0x0E, 0x35);// GPIO 2 and 1
    i2c_smbus_write_byte_data(data->client, 0x0D, 0x05);// GPIO 0

    return 0;
}

static int serdes_948_init(struct serdes_941_948_data *data)
{
    //ds90ub948  Device initialization
    pr_info("%s\n",__func__);

    //GENERAL_CONFIGURATION_1
    i2c_smbus_write_byte_data(data->client, 0x03, 0xF8);

    //I2C_CONTROL_1
    i2c_smbus_write_byte_data(data->client, 0x05, 0x9E);

    //GPIO config
    i2c_smbus_write_byte_data(data->client, 0x1F, 0x05);// GPIO 3
    i2c_smbus_write_byte_data(data->client, 0x1E, 0x53);// GPIO 2 and 1
    i2c_smbus_write_byte_data(data->client, 0x1D, 0x03);// GPIO 0

    //CML_OUTPUT
    i2c_smbus_write_byte_data(data->client, 0x56, 0x08);
    i2c_smbus_write_byte_data(data->client, 0x57, 0x02);

    return 0;

}

static int serdes_941_948_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
    struct serdes_941_948_data *data;
    pr_info("%s : %d SERDES \n",__func__, __LINE__);

    data = devm_kzalloc(&client->dev, sizeof(struct serdes_941_948_data),
            GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    data->client  = client;
    i2c_set_clientdata(client, data);
    //i2c_smbus_read_byte_data(client, 0x00);

    //read register 0x00 if value is 0x18 call serdes_941_init() else if 0x58 call serdes_948_init();
    serdes_941_init(data);
    data->client->addr = DES_ADDRESS;
    serdes_948_init(data);

    return 0;
}

static int serdes_941_948_remove(struct i2c_client *client)
{
    return 0;
}
#ifdef CONFIG_OF
static struct of_device_id serdes_941_948_dt_match[] = {
    { .compatible = "ti,serdes_941", },
    {},
};
#endif
MODULE_DEVICE_TABLE(of, serdes_941_948_dt_match);
static const struct i2c_device_id serdes_941_948_id[] = {
    {"serdes_941", 0},
    {"serdes_948", 1},
    {},
};
MODULE_DEVICE_TABLE(i2c, serdes_941_948_id);

static struct i2c_driver serdes_941_948_driver = {
    .driver = {
        .owner  = THIS_MODULE,
        .name   = "serdes_941_948",
        .of_match_table = serdes_941_948_dt_match,
    },
    .probe          = serdes_941_948_probe,
    .remove         = serdes_941_948_remove,
    .id_table       = serdes_941_948_id,
};

static int __init serdes_941_948_init(void)
{
    pr_info("%s : %d SERDES \n",__func__, __LINE__);
    return i2c_add_driver(&serdes_941_948_driver);
}
module_init(serdes_941_948_init);

static void __exit serdes_941_948_exit(void)
{
    i2c_del_driver(&serdes_941_948_driver);
}
module_exit(serdes_941_948_exit);

MODULE_AUTHOR("Tessolve");
MODULE_DESCRIPTION("TI serdes_941_948 driver");
MODULE_LICENSE("GPL v2");
