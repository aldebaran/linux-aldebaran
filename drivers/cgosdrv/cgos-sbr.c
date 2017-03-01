#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sysfs.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>

#include "DrvOsHdr.h"
#include "CgosDrv.h"
#include "CgosIoct.h"

#define DRIVER_AUTHOR	"Julien Massot <jmassot@softbankrobotics.com>"
#define DRIVER_DESC	"CGOS SBR driver"
#define DRIVER_NAME	"cgos-sbr"

enum robot_type {
	ROBOT_TYPE_UNKNOWN = 0,
	ROBOT_TYPE_PEPPER_1 = 1,
	ROBOT_TYPE_PEPPER_18 = 2,
	ROBOT_TYPE_NAO_6 = 3,
};

#define HEAD_ID_OFFSET 12
#define HEAD_ID_LEN 20
#define MACHINE_ID_LEN 32

/* length of the string plus termination '\0' */
static char head_id[ HEAD_ID_LEN + 1 ];
static char sbr_machine_id[ MACHINE_ID_LEN + 1 ];
static enum robot_type robot_type = ROBOT_TYPE_UNKNOWN;

static ssize_t machineid_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, MACHINE_ID_LEN + 2, "%s\n", sbr_machine_id);
}

static ssize_t head_id_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, head_id);
}

static ssize_t robot_type_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	switch (robot_type) {
	case ROBOT_TYPE_PEPPER_1:
		return sprintf(buf, "pepper1");
	case ROBOT_TYPE_PEPPER_18:
		return sprintf(buf, "pepper18");
	case ROBOT_TYPE_NAO_6:
		return sprintf(buf, "nao6");
	case ROBOT_TYPE_UNKNOWN:
		break;
	}
	return sprintf(buf, "unknwon");
}

static struct kobject *kobj;
static struct kobj_attribute sc_attrb =
	__ATTR(head_id, 0444, head_id_show, NULL);

static struct kobj_attribute sc_attrb_machine_id =
	__ATTR(machineid, 0444, machineid_show, NULL);

static struct kobj_attribute sc_attrib_robot_type =
	__ATTR(robot_type, 0444, robot_type_show, NULL);

#define MD5_SIGNATURE_SIZE 16

static int compute_machine_id(void)
{
	struct scatterlist sg;
	struct hash_desc desc;
	char buf[HEAD_ID_LEN];
	u8 hashval[MD5_SIGNATURE_SIZE];
	int err, i;

	memcpy(buf, head_id, HEAD_ID_LEN);
	memset(hashval, 0, 20);

	desc.tfm = crypto_alloc_hash("md5", 0, CRYPTO_ALG_ASYNC);
	if (desc.tfm == NULL)
		return -ENOMEM;

	desc.flags = 0;
	sg_init_one(&sg, buf, HEAD_ID_LEN);

	err = crypto_hash_digest(&desc, &sg, HEAD_ID_LEN, hashval);
	if (err) {
		printk(KERN_ERR "Fail to compute machine id\n");
		goto error;
	}

	for (i = 0; i < MD5_SIGNATURE_SIZE; i++)
		sprintf(&sbr_machine_id[2*i], "%02x", hashval[i]);

  error:
	crypto_free_hash(desc.tfm);
	return err;
}

static enum robot_type get_robot_type(const char *head_id)
{
	if (strncmp(head_id, "AP990237", 8) == 0)
		return ROBOT_TYPE_PEPPER_1;

	if (strncmp(head_id, "AP990397", 8) == 0)
		return ROBOT_TYPE_PEPPER_18;

	if (strncmp(head_id, "P0000074", 8) == 0)
		return ROBOT_TYPE_NAO_6;

	return ROBOT_TYPE_UNKNOWN;
}

static int read_head_id(unsigned int handle)
{
	CGOSIOCTLIN in_hdr;
	CGOSIOCTLOUT *out_hdr;
	unsigned int out_len;
	unsigned int buflen = sizeof(CGOSIOCTLOUT) + HEAD_ID_LEN + HEAD_ID_OFFSET;
	unsigned char buffer[buflen];
	unsigned char *ptr;

	memset(head_id, 0, HEAD_ID_LEN);

	in_hdr.fct = xCgosStorageAreaRead;
	in_hdr.handle = handle;

	in_hdr.type = CGOS_STORAGE_AREA_EEPROM;

	/* offset */
	in_hdr.pars[0] = 0;
	/* length */
	in_hdr.pars[1] = HEAD_ID_LEN + HEAD_ID_OFFSET;
	in_hdr.pars[2] = 0;
	in_hdr.pars[3] = 0;

	cgos_issue_request((unsigned int) CGOS_IOCTL, (unsigned int *) &in_hdr,
			sizeof(CGOSIOCTLIN), (unsigned int *) buffer,
			buflen, &out_len);

	out_hdr = (CGOSIOCTLOUT *) buffer;
	if (out_len != buflen || out_hdr->rets[0] != (HEAD_ID_LEN + HEAD_ID_OFFSET)) {
		printk(KERN_ERR "Fail to read from eemprom\n");
		return -EIO;
	}

	/* skip header to retrieve data */
	ptr = buffer + sizeof(CGOSIOCTLOUT);

	if (strncmp(ptr, "ALDE00000000", HEAD_ID_OFFSET)) {
	  printk(KERN_ERR "Fail to read head id from eeprom\n");
	  return -EIO;
	}

	memcpy(head_id, ptr + HEAD_ID_OFFSET, HEAD_ID_LEN);
	head_id[HEAD_ID_LEN] = '\0';
	return 0;
}

static int cgos_sbr_board_open(unsigned int *handle)
{
	CGOSIOCTLIN in_hdr;
	CGOSIOCTLOUT out_hdr;
	unsigned int out_len;

	memset(&in_hdr, 0, sizeof(CGOSIOCTLIN));
	in_hdr.fct = xCgosBoardOpen;

	cgos_issue_request((unsigned int) CGOS_IOCTL, (unsigned int *) &in_hdr,
			   sizeof(CGOSIOCTLIN), (unsigned int *) &out_hdr,
			   sizeof(CGOSIOCTLOUT), &out_len);

	if (out_len < sizeof(CGOSIOCTLOUT)) {
		return -EIO;
	}

	*handle = out_hdr.rets[0];
	return 0;
}

static void cgos_sbr_board_close(unsigned int handle)
{
	CGOSIOCTLIN in_hdr;

	memset(&in_hdr, 0, sizeof(CGOSIOCTLIN));
	in_hdr.fct     = xCgosBoardClose;
	in_hdr.handle  = handle;

	cgos_issue_request((unsigned int) CGOS_IOCTL, (unsigned int *) &in_hdr,
			   sizeof(CGOSIOCTLIN), NULL,
			   0, NULL);
}

static int __init cgos_sbr_init(void)
{
	unsigned int handle;
	int err;

	err = cgos_sbr_board_open(&handle);
	if (err)
		return err;

	err = read_head_id(handle);
	if (err)
		goto error;

	err = compute_machine_id();
	if (err)
		goto error;

	robot_type = get_robot_type(head_id);

	kobj = kobject_create_and_add("qi", NULL);
	if (!kobj) {
		err = -ENOMEM;
		goto error;
	}

	/* Ignore error for sysfs_create_file */
	err = sysfs_create_file(kobj, &sc_attrb.attr);
	if (err)
		goto error;
	err = sysfs_create_file(kobj, &sc_attrb_machine_id.attr);
	if (err)
		goto error;
	err = sysfs_create_file(kobj, &sc_attrib_robot_type.attr);
  error:
	cgos_sbr_board_close(handle);
	return err;
}

static void __exit cgos_sbr_exit(void)
{
	if (kobj)
		kobject_put(kobj);
}

module_init(cgos_sbr_init);
module_exit(cgos_sbr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
