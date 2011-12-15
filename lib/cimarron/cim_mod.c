/* <LIC_AMD_STD>
 * Copyright (c) 2003-2005 Advanced Micro Devices, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING
 * </LIC_AMD_STD>  */
/* <CTL_AMD_STD>
 * </CTL_AMD_STD>  */
/* <DOC_AMD_STD>
 * Cimarron module initalization
 * Jordan Crouse (jordan.crouse@amd.com)
 * </DOC_AMD_STD>  */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "cim_mem.h"
#include "cim_dev.h"
#include "build_num.h"

#ifdef MODULE
MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("AMD Geode LX Graphics Abstraction Layer");
MODULE_LICENSE("GPL");
#endif

#define CIM_MOD_DRIVER_NAME "cimarron"

int cim_init_dev(void);
void cim_exit_dev(void);

int cim_init_memory(void);
void cim_exit_memory(void);
/* We do this because there really isn't any way to verify that the
   KFB or V4L2 is going to be loaded before Cimarron is.  We force
   the issue by asking the other entities to call cim_mod_init()
   and we set a flag indicating that we have already been set up
*/

static int cim_init_flag = 0;

static int
cim_mod_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid) {
  int ret;
  ret = pci_enable_device(pcidev);
  if (ret) return ret;
  ret = cim_init_memory();
  if (ret) return ret;

  ret = cim_init_dev();
  if (ret) {
	cim_exit_memory();
	return ret;
  }
  return 0;
}

static void
cim_mod_remove(struct pci_dev *dev) {
  cim_exit_memory();
  cim_exit_dev();
}

static struct
pci_device_id cim_mod_id_table[] __devinitdata = {
  {PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LX_VIDEO, \
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
  {0,}
};

MODULE_DEVICE_TABLE(pci, cim_mod_id_table);

static struct
pci_driver cim_mod_driver = {
  name:      CIM_MOD_DRIVER_NAME,
  id_table:  cim_mod_id_table,
  probe:     cim_mod_probe,
  remove:    cim_mod_remove
};

int __init
cim_mod_init(void)
{
  int ret;
  if( cim_init_flag != 0 ) return 0;
  printk(KERN_INFO "%s\n", AMD_VERSION);
  ret = pci_register_driver(&cim_mod_driver);
  if( ret == 0 ) cim_init_flag = 1;
  return ret;
}

#ifdef MODULE
void __exit
cim_mod_exit(void)
{
  printk(KERN_INFO CIM_MOD_DRIVER_NAME " unloading\n");
  pci_unregister_driver(&cim_mod_driver);
  cim_init_flag = 0;
}
#endif

module_init(cim_mod_init);

#ifdef MODULE
module_exit(cim_mod_exit);
#endif
