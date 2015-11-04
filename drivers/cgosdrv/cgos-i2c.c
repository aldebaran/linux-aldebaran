#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "DrvOsHdr.h"
#include "CgosDrv.h"
#include "Cgos.h"

#define DRIVER_AUTHOR	"Samuel Martin <smartin@aldebaran.com>"
#define DRIVER_DESC	"CGOS I2C driver"
#define DRIVER_NAME	"cgos-i2c"

/* debug wrapper */
#ifdef DEBUG
#ifdef CGOS_I2C_LOG_SHOW_FUNC
#define cgos_dbg(level, fmt, args...) \
	printk(level DRIVER_NAME ": %s: " fmt, __func__, ## args)
#else
#define cgos_dbg(level, fmt, args...) \
	printk(level DRIVER_NAME ": " fmt, ## args)
#endif
#else
#define cgos_dbg(args...)
#endif

#define CGBUF_LEN       512	/* lucky guess, no packet larger than 256 bytes
				 * + the data for cgos access (<= 32 bytes) */
#define CGDATA_LEN_IN   4
#define CGDATA_LEN_OUT  2
struct cgos_payload_header_wr {
	uint len;
	/* start of data passed to cgos_issue_request */
	uint fct;
	uint handle;
	uint type;
	uint data[CGDATA_LEN_IN];
};
struct cgos_payload_header_rd {
	uint len;
	uint response_len;
	/* start of data passed to cgos_issue_request */
	uint status;
	uint data[CGDATA_LEN_OUT];
};

struct cgos_i2c_adapter {
	struct i2c_adapter adapter;
	uint handle;
	uint interface;
	struct mutex mutex;
	u8 wr_buf[CGBUF_LEN], rd_buf[CGBUF_LEN];
	struct list_head sibling;
};

static DEFINE_MUTEX(cgos_mutex);
static LIST_HEAD(cgos_i2c_adapters);

enum cgos_fct {
	cgos_release_handle = 1,
	cgos_request_handle = 3,
	cgos_i2c_count = 31,
	cgos_i2c_type = 32,
	cgos_i2c_is_available = 33,
	cgos_i2c_read = 34,
	cgos_i2c_write = 35,
	cgos_i2c_read_register = 36,
	cgos_i2c_write_register = 37,
	cgos_i2c_write_read_combined = 38,
	cgos_i2c_get_max_frequency = 85,
	cgos_i2c_get_frequency = 86,
	cgos_i2c_set_frequency = 87
};

#ifdef DEBUG
#define dump_device(d)  _dump_device(d)
static void _dump_device(struct cgos_i2c_adapter *dev)
{
	cgos_dbg(KERN_INFO,
		 "@dev=0x%pK, dev->adapter.nr=%u dev->handle=%lu "
		 "dev->interface=%lu\n",
		 dev, dev->adapter.nr, dev->handle, dev->interface);
}
#else
#define dump_device(d)
#endif


/* cgos_issue_request wrapper */
static int cgos_do_request(struct cgos_payload_header_wr *wr_buf,
		struct cgos_payload_header_rd *rd_buf)
{
	/* skip the length fields in the r/w buffer passed to
	 * cgos_issue_request */
	uint *wr_data = (uint *) ( ((u8*) wr_buf) +
		offsetof(struct cgos_payload_header_wr, fct) );
	uint *rd_data = (uint *) ( ((u8*) rd_buf) +
		offsetof(struct cgos_payload_header_rd, status) );
	return cgos_issue_request((uint) CGOS_IOCTL, \
		wr_data, wr_buf->len, \
		rd_data, rd_buf->len, &(rd_buf->response_len));
}

/* cgos_payload_header_* structures sizes as passed to cgos_issue_request */
static const uint rd_hdr_len =
	sizeof(struct cgos_payload_header_rd) -
	offsetof(struct cgos_payload_header_rd, status);
static const uint wr_hdr_len =
	sizeof(struct cgos_payload_header_wr) -
	offsetof(struct cgos_payload_header_wr, fct);

static int cgos_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	struct cgos_i2c_adapter *dev =
	    container_of(adap, struct cgos_i2c_adapter, adapter);
	int ret = 0, i;
	int err = 0;

	uint rd_lmax = 0, wr_lmax = 0;
	u8 *rd_buf, *wr_buf, *rd_data, *wr_data;
	struct cgos_payload_header_rd *rd_hdr;
	struct cgos_payload_header_wr *wr_hdr;

	/* instanciate 2 buffers for all transferts */
	for (i = 0; i < num; i++) {
		if (!(msgs[i].flags & I2C_M_RD))
			rd_lmax = max((uint) rd_lmax, (uint) msgs[i].len);
		else
			wr_lmax = max((uint) wr_lmax, (uint) msgs[i].len);
	}
	rd_lmax += rd_hdr_len;
	wr_lmax += wr_hdr_len;

	cgos_dbg(KERN_INFO, "#msg=%d \trd_lmax=%lu \twr_lmax=%lu\n",
		 num, rd_lmax, wr_lmax);

	mutex_lock(&dev->mutex);

	/* use the device's buffer if large enough, otherwise allocate them */
	rd_buf = &dev->rd_buf[0];
	if (rd_lmax > CGBUF_LEN) {
		rd_buf = kzalloc(rd_lmax, GFP_KERNEL);
		if (!rd_buf) {
			ret = -ENOMEM;
			goto exit_now;
		}
	}
	wr_buf = &dev->wr_buf[0];
	if (wr_lmax > CGBUF_LEN) {
		wr_buf = kzalloc(wr_lmax, GFP_KERNEL);
		if (!wr_buf) {
			ret = -ENOMEM;
			goto free_rd_buf;
		}
	}

	/* for ease of use */
	rd_hdr = (struct cgos_payload_header_rd *)rd_buf;
	rd_data = (u8 *) rd_buf + sizeof(struct cgos_payload_header_rd);
	wr_hdr = (struct cgos_payload_header_wr *)wr_buf;
	wr_data = (u8 *) wr_buf + sizeof(struct cgos_payload_header_wr);

	for (i = 0; i < num; ++i) {
		/* initialize r/w buffers */
		memset(rd_buf, 0, max(rd_lmax, (uint) CGBUF_LEN));
		memset(wr_buf, 0, max(wr_lmax, (uint) CGBUF_LEN));
		rd_hdr->len = rd_hdr_len;
		wr_hdr->handle = dev->handle;
		wr_hdr->type = dev->interface;
		wr_hdr->data[0] = (msgs[i].addr << 1);
		wr_hdr->len = wr_hdr_len;

		if (msgs[i].flags & I2C_M_RD) {
			wr_hdr->fct = cgos_i2c_read;
			wr_hdr->data[0] |= 0x1;
			rd_hdr->len += msgs[i].len;
		} else {
			wr_hdr->fct = cgos_i2c_write;
			wr_hdr->len += msgs[i].len;
			memcpy(&wr_data[0], &msgs[i].buf[0], msgs[i].len);
		}

		/* arbitrary dump the 48 first bytes of the message */
		cgos_dbg(KERN_INFO,
			 "xfer %2d/%2d: %-5s @0x%0hx (0x%0hx) len=%d "
			 "data=[%48phN]\n",
			 i + 1, num,
			 (msgs[i].flags & I2C_M_RD) ? "read" : "write",
			 (u16) (wr_hdr->data[0] & 0xffff), msgs[i].addr, msgs[i].len,
			 (msgs[i].len > 48) ? NULL : msgs[i].buf);
		ret = cgos_do_request(wr_hdr, rd_hdr);

		if (ret) {
			/* request exit with error */
			cgos_dbg(KERN_ERR, "request error!\n");
			err++;
			continue;
		}

		if (rd_hdr->response_len < rd_hdr_len) {
			/* transaction failure */
			cgos_dbg(KERN_ERR, "transfer error!\n");
			err++;
			continue;
		}

		if (rd_hdr->status != 0) {
			/* transaction exit with error */
			cgos_dbg(KERN_ERR, "transfer error!\n");
			err++;
			continue;
		}

		if (msgs[i].flags & I2C_M_RD) {
			uint rd_len = rd_hdr->response_len - rd_hdr_len;
			uint cpy_len =
			    min((uint) msgs[i].len, (uint) rd_len);
			if (msgs[i].len < rd_len)
				/* more data read than requested,
				 * user buffer not large enough */
				cgos_dbg(KERN_WARNING,
					 "read more data than expected: %d, "
					 "got %lu. %lu bytes returned\n",
					 msgs[i].len, rd_len, cpy_len);
			if (msgs[i].len > rd_len)
				/* less data read than requested,
				 * user buffer too large */
				cgos_dbg(KERN_WARNING,
					 "read less data than expected: %d, "
					 "got %lu. %lu bytes returned\n",
					 msgs[i].len, rd_len, cpy_len);
			memcpy(&msgs[i].buf[0], &rd_data[0], cpy_len);
		}

	}

	/* return the number of successfully processed messages */
	ret = num - err;

	if (wr_lmax > CGBUF_LEN)
		kfree(wr_buf);
free_rd_buf:
	if (rd_lmax > CGBUF_LEN)
		kfree(rd_buf);
exit_now:
	mutex_unlock(&dev->mutex);
	return ret;
}

static u32 cgos_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_10BIT_ADDR | I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm cgos_i2c_algo = {
	.master_xfer = cgos_i2c_master_xfer,
	.functionality = cgos_i2c_func,
};

static inline int cgos_i2c_adapter_create(uint handle, uint interface)
{
	int r;
	struct cgos_i2c_adapter *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		cgos_dbg(KERN_ERR,
			 "Cannot allocate device for i2c interface %lu\n",
			 interface);
		r = -ENOMEM;
		goto exit_now;
	}

	dev->handle = handle;
	dev->interface = interface;
	mutex_init(&dev->mutex);

	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_HWMON;
	dev->adapter.algo = &cgos_i2c_algo;
	strcpy(dev->adapter.name, "I2C primary cgos");

	r = i2c_add_adapter(&dev->adapter);
	if (r) {
		cgos_dbg(KERN_ERR,
			 "Cannot register adapter for i2c interface %lu\n",
			 interface);
		goto free_dev;
	}

	mutex_lock(&cgos_mutex);
	list_add(&dev->sibling, &cgos_i2c_adapters);
	mutex_unlock(&cgos_mutex);

	dump_device(dev);

	cgos_dbg(KERN_INFO, "cgos i2c bus added (i2c-%d) for interface %lu\n",
		 dev->adapter.nr, interface);
	return 0;

free_dev:
	kfree(dev);
exit_now:
	return r;
}

static int __init cgos_i2c_init(void)
{
	uint bus_cnt, i;
	int r, ret = 0;
	struct list_head *p;
	struct cgos_payload_header_rd rd_hdr;
	struct cgos_payload_header_wr wr_hdr;

	/* get handle */
	memset(&rd_hdr, 0, sizeof(rd_hdr));
	memset(&wr_hdr, 0, sizeof(wr_hdr));
	rd_hdr.len = rd_hdr_len;
	wr_hdr.fct = cgos_request_handle;
	wr_hdr.len = wr_hdr_len;
	ret = cgos_do_request(&wr_hdr, &rd_hdr);
	if (ret || rd_hdr.response_len < rd_hdr_len)
		return -EREMOTEIO;

	wr_hdr.handle = rd_hdr.data[0];

	/* setup i2c adapters */
	memset(&rd_hdr, 0, sizeof(rd_hdr));
	rd_hdr.len = rd_hdr_len;
	wr_hdr.fct = cgos_i2c_count;
	ret = cgos_do_request(&wr_hdr, &rd_hdr);
	if (ret || rd_hdr.response_len < rd_hdr_len)
		return -EREMOTEIO;

	bus_cnt = rd_hdr.data[0];
	cgos_dbg(KERN_DEBUG, "%lu i2c-bus found\n", bus_cnt);
	for (i = 0; i < bus_cnt; ++i) {
		memset(&rd_hdr, 0, sizeof(rd_hdr));
		rd_hdr.len = rd_hdr_len;
		wr_hdr.type = i;
		wr_hdr.fct = cgos_i2c_type;
		r = cgos_do_request(&wr_hdr, &rd_hdr);
		if (r || rd_hdr.response_len < rd_hdr_len)
			return -EREMOTEIO;
		/* only consider "primary" type busses */
		if (rd_hdr.data[0] != CGOS_I2C_TYPE_PRIMARY)
			continue;
		memset(&rd_hdr, 0, sizeof(rd_hdr));
		rd_hdr.len = rd_hdr_len;
		wr_hdr.fct = cgos_i2c_is_available;
		r = cgos_do_request(&wr_hdr, &rd_hdr);
		if (r)
			continue;
		/* instanciate the adapter */
		r = cgos_i2c_adapter_create(wr_hdr.handle, i);
		if (r) {
			cgos_dbg(KERN_ERR,
				 "Cannot create cgos i2c bus for unit %lu\n",
				 i);
			if (!ret)
				ret = r;
		}
	}

	bus_cnt = 0;
	list_for_each(p, &cgos_i2c_adapters) {
		bus_cnt++;
	}
	cgos_dbg(KERN_ERR, "%lu cgos-i2c bus created\n", bus_cnt);
	return ret;
}

static inline void cgos_i2c_adapter_remove(struct cgos_i2c_adapter *dev)
{
#ifdef DEBUG
	int nr = dev->adapter.nr;
	uint interface = dev->interface;
#endif
	/* lock already hold */
	i2c_del_adapter(&dev->adapter);
	kfree(dev);
	cgos_dbg(KERN_INFO, "cgos i2c bus removed (i2c-%d) for interface %lu\n",
		 nr, interface);
}

static void __exit cgos_i2c_exit(void)
{
	struct cgos_i2c_adapter *dev =
	    list_first_entry(&cgos_i2c_adapters, struct cgos_i2c_adapter,
			     sibling);
	struct cgos_payload_header_wr wr_hdr;
	struct cgos_payload_header_rd rd_hdr;
	int ret;

	memset(rd_hdr.data, 0x0, sizeof(rd_hdr.data));
	rd_hdr.len = rd_hdr_len;
	memset(wr_hdr.data, 0x0, sizeof(wr_hdr.data));
	wr_hdr.handle = dev->handle;
	wr_hdr.fct = cgos_release_handle;
	wr_hdr.type = 0;
	wr_hdr.len = wr_hdr_len;

	mutex_lock(&cgos_mutex);
	list_for_each_entry(dev, &cgos_i2c_adapters, sibling)
	    cgos_i2c_adapter_remove(dev);
	mutex_unlock(&cgos_mutex);

	/* release handle */
	cgos_dbg(KERN_DEBUG, "Releasing handle...\n");
	ret = cgos_do_request(&wr_hdr, &rd_hdr);
	if (ret || rd_hdr.response_len < rd_hdr_len)
		cgos_dbg(KERN_ERR, "Could not release HW\n");
}

module_init(cgos_i2c_init);
module_exit(cgos_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
