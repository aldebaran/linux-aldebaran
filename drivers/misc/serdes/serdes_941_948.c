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
};

static int serdes_941_init(struct serdes_941_948_data *data)
{
    int ret;

    //ds90ub941  Device initialization
    pr_info("%s\n",__func__);

    //Disable DSI
    ret = i2c_smbus_write_byte_data(data->client, 0x01, 0x08);
    if (ret < 0)
        pr_err("%s error setting DISABLE_DSI\n",__func__);

    //Remote slave devices
    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_0,    REMOTE_SLAVE_ADD_1);
    if (ret < 0)
        pr_err("%s error setting NFC I2C\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_0, REMOTE_SLAVE_ADD_1);
    if (ret < 0)
        pr_err("%s error setting NFC I2C ALIAS\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_1,    REMOTE_SLAVE_ADD_2);
    if (ret < 0)
        pr_err("%s error setting BACKLIGHT I2C\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_1, REMOTE_SLAVE_ADD_2);
    if (ret < 0)
        pr_err("%s error setting BACKLIGHT I2C ALIAS\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_2,    REMOTE_SLAVE_ADD_3);
    if (ret < 0)
        pr_err("%s error setting TOUCH I2CLINK_ERROR_COUNT\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_2, REMOTE_SLAVE_ADD_3);
    if (ret < 0)
        pr_err("%s error setting TOUCH I2C ALIAS\n",__func__);


    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ID_3, REMOTE_SLAVE_ADD_4);
    if (ret < 0)
        pr_err("%s error setting EEPROM I2C\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, REG_SLAVE_ALIAS_3, REMOTE_SLAVE_ADD_4);
    if (ret < 0)
        pr_err("%s error setting EEPROM I2C ALIAS\n",__func__);

    //GENERAL_CFG
    ret = i2c_smbus_write_byte_data(data->client, 0x03, 0x9A);
    if (ret < 0)
        pr_err("%s error setting GENERAL_CFG\n",__func__);

    //Disable I2S
    i2c_smbus_write_byte_data(data->client, 0x54, 0x00);

    //GPIO config
    ret = i2c_smbus_write_byte_data(data->client, 0x0F, 0x03);// GPIO 3
    if (ret < 0)
        pr_err("%s error setting GPIO 3\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, 0x0E, 0x35);// GPIO 2 and 1
    if (ret < 0)
        pr_err("%s error setting GPIO 2 AND 1\n",__func__);

    ret = i2c_smbus_write_byte_data(data->client, 0x0D, 0x05);// GPIO 0
    if (ret < 0)
        pr_err("%s error setting GPIO 0\n",__func__);

    /*************************/
    /* Panel configuration   */
    /*************************/

    // Total Horizontal Width:
    i2c_smbus_write_byte_data(data->client, 0x66, 0x04);
    i2c_smbus_write_byte_data(data->client, 0x67, 0xc0);

    i2c_smbus_write_byte_data(data->client, 0x66, 0x05);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x93);

    i2c_smbus_write_byte_data(data->client, 0x66, 0x06);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x52);

    i2c_smbus_write_byte_data(data->client, 0x66, 0x07);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x20);

    i2c_smbus_write_byte_data(data->client, 0x66, 0x08);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x03);
    i2c_smbus_write_byte_data(data->client, 0x66, 0x09);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x50);

    // horizontal sync width
    i2c_smbus_write_byte_data(data->client, 0x66, 0x0A);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x18);

    // vertical sync width
    i2c_smbus_write_byte_data(data->client, 0x66, 0x0B);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x03);

    // horizontal back porch width
    i2c_smbus_write_byte_data(data->client, 0x66, 0x0C);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x38);

    // vertical back porch width
    i2c_smbus_write_byte_data(data->client, 0x66, 0x0D);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x14);

    // frequency N : 800 * M / N
    i2c_smbus_write_byte_data(data->client, 0x66, 0x03);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x34);

    // frequency M : 800 * M / N
    i2c_smbus_write_byte_data(data->client, 0x66, 0x1A);
    i2c_smbus_write_byte_data(data->client, 0x67, 0x05);

    // Set DSI register 0x05 to 0x12
    i2c_smbus_write_byte_data(data->client, 0x40, 0x04);
    i2c_smbus_write_byte_data(data->client, 0x41, 0x05);
    i2c_smbus_write_byte_data(data->client, 0x42, 0x12);

    // Use DSI CLOCK
    i2c_smbus_write_byte_data(data->client, 0x56, 0x00);

    //Enable DSI
    ret = i2c_smbus_write_byte_data(data->client, 0x01, 0x00);
    if (ret < 0)
        pr_err("%s error setting ENABLE DSI\n",__func__);

    return 0;
}

static int serdes_948_init(struct serdes_941_948_data *data)
{
    int ret;

    //ds90ub948  Device initialization
    pr_info("%s\n",__func__);

    //LINK_ERROR_COUNT
    ret = i2c_smbus_write_byte_data(data->client, 0x41, 0x1F);
    if (ret < 0)
    {
        pr_err("%s error setting LINK_ERROR_COUNT\n",__func__);
        goto err;
    }

    //GENERAL_CONFIGURATION_1
    ret = i2c_smbus_write_byte_data(data->client, 0x03, 0xf0);
    if (ret < 0)
    {
        pr_err("%s error setting GENERAL_CONFIGURATION_1\n",__func__);
        goto err;
    }

    //I2C_CONTROL_1
    ret = i2c_smbus_write_byte_data(data->client, 0x05, 0x1E);
    if (ret < 0)
    {
        pr_err("%s error setting I2C_CONTROL_1\n",__func__);
        goto err;
    }

    //GPIO config
    ret = i2c_smbus_write_byte_data(data->client, 0x1F, 0x05);// GPIO 3
    if (ret < 0)
    {
        pr_err("%serror setting GPIO 3 \n",__func__);
        goto err;
    }
    ret = i2c_smbus_write_byte_data(data->client, 0x1E, 0x53);// GPIO 2 and 1
    if (ret < 0)
    {
        pr_err("%serror setting GPIO 2 and 1\n",__func__);
        goto err;
    }
    ret = i2c_smbus_write_byte_data(data->client, 0x1D, 0x03);// GPIO 0
    if (ret < 0)
    {
        pr_err("%s error setting GPIO 0 \n",__func__);
        goto err;
    }

    return 0;

err:
    return ret;
}

static int serdes_941_948_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
    int ret;
    struct serdes_941_948_data *data;
    pr_info("%s : %d SERDES \n",__func__, __LINE__);

    data = devm_kzalloc(&client->dev, sizeof(struct serdes_941_948_data),
            GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    data->client  = client;
    i2c_set_clientdata(client, data);
    //i2c_smbus_read_byte_data(client, 0x00);

    ret = serdes_941_init(data);
    if (ret < 0)
    {
	    pr_err("%s error could not initialize serialiser (%d)\n",__func__, ret);
    }
    // Wait 100ms that SER initialises
    msleep(100);
    data->client->addr = DES_ADDRESS;
    ret = serdes_948_init(data);
    if (ret < 0)
    {
	    pr_err("%s error could not initialize deserialiser (%d)\n",__func__, ret);
    }
    // Wait 50ms that DES initialises
    msleep(50);

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
