#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/platform_device.h>


static struct platform_device *pdev = NULL;
static struct uio_info *info = NULL;
static void *mem = NULL;


static int __init memmap_init(void)
{
	phys_addr_t mem_phys;

	pr_info("uio_memmap: init start\n");

	pdev = platform_device_register_simple("uio_memmap", 0, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("Failed to register platform device\n");
		return PTR_ERR(pdev);
	}

	info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
	if (!info) {
		pr_err("Failed to allocate uio_info memory\n");
		return -ENOMEM;
	}

	mem = kzalloc(0x100000, GFP_KERNEL);
	if (!mem) {
		pr_err("Failed to allocate mem memory\n");
		goto free_info;
	}

	mem_phys = virt_to_phys(mem);

	pr_info("uio_memmap: Allocated memory:\nVIRT: 0x%016llx\nPHYS: 0x%016llx\n", (phys_addr_t)mem, mem_phys);

	info->name = "memmap";
	info->version = "0.1";

	info->irq = UIO_IRQ_NONE;

	info->mem[0].name = "mem0";
	info->mem[0].addr = mem_phys;
	info->mem[0].internal_addr = mem;
	info->mem[0].size = 0x100000;
	info->mem[0].memtype = UIO_MEM_PHYS;

	if (uio_register_device(&pdev->dev, info)) {
		pr_err("Failed to register UIO device\n");
		goto free_mem;
	}

	pr_info("uio_memmap: init done\n");

	return 0;

free_mem:
	kfree(mem);
free_info:
	kfree(info);
	return -ENODEV;
}

static void __exit memmap_exit(void)
{
	uio_unregister_device(info);
	kfree(info);
	kfree(mem);
	platform_device_unregister(pdev);

	pr_info("uio_memmap: exited\n");
}


module_init(memmap_init);
module_exit(memmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Russell Joyce");
MODULE_DESCRIPTION("UIO-based Memory Mapping");
MODULE_VERSION("0.1");
