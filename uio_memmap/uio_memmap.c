#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>


#define DYNAMIC_MAP_SIZE 0x40000000


struct uio_memmap_platdata {
    struct uio_info *uioinfo;
    struct platform_device *pdev;
    bool open;
};


static int uio_memmap_open(struct uio_info *info, struct inode *inode)
{
	int ret = 0;
	struct uio_memmap_platdata *priv = info->priv;
	struct platform_device *pdev = priv->pdev;
	void *mem = NULL;
	dma_addr_t mem_phys = 0x40000000;

	if (priv->open) {
		dev_err(&pdev->dev, "Device is already open\n");
		return -EBUSY;
	}

	mem = dma_zalloc_coherent(&pdev->dev, DYNAMIC_MAP_SIZE, &mem_phys, GFP_KERNEL);
	if (!mem) {
		dev_err(&pdev->dev, "Failed to allocate mem memory\n");
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "uio_memmap_open - Allocated DMA mem:\n    VIRT: 0x%016llx\n    PHYS: 0x%016llx\n", (u64)mem, mem_phys);

	info->mem[0].addr = mem_phys;
	info->mem[0].internal_addr = mem;

	priv->open = true;

	return ret;
}

static int uio_memmap_release(struct uio_info *info, struct inode *inode)
{
	struct uio_memmap_platdata *priv = info->priv;
	struct platform_device *pdev = priv->pdev;

	dma_free_coherent(&pdev->dev, DYNAMIC_MAP_SIZE, info->mem[0].internal_addr, info->mem[0].addr);

	info->mem[0].addr = 0;
	info->mem[0].internal_addr = 0;

	priv->open = false;

	dev_info(&pdev->dev, "uio_memmap_release - Freed DMA mem\n");

	return 0;
}

static int uio_memmap_probe(struct platform_device *pdev)
{
	struct uio_info *uioinfo = dev_get_platdata(&pdev->dev);
	struct uio_memmap_platdata *priv;
	int ret = -EINVAL;
//	void *mem = NULL;
//	dma_addr_t mem_phys;
	int err;

	dev_info(&pdev->dev, "probe start\n");

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No device tree node\n");
		ret = -ENODEV;
		goto error0;
	}

	uioinfo = devm_kzalloc(&pdev->dev, sizeof(*uioinfo), GFP_KERNEL);
	if (!uioinfo) {
		dev_err(&pdev->dev, "Failed to allocate uio_info memory\n");
		ret = -ENOMEM;
		goto error0;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to kmalloc\n");
		ret = -ENOMEM;
		goto error0;
	}

	priv->uioinfo = uioinfo;
	priv->pdev = pdev;
	priv->open = false;
	uioinfo->priv = priv;

	/* Initialize reserved memory resources */
	err = of_reserved_mem_device_init(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Could not get reserved memory: %d\n", err);
		ret = -ENOMEM;
		goto error0;
	}

	/* Allocate memory */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "Could not set coherent mask: %d\n", err);
		ret = -ENOMEM;
		goto error1;
	}

//	mem = dma_alloc_coherent(&pdev->dev, DYNAMIC_MAP_SIZE, &mem_phys, GFP_KERNEL);
//	if (!mem) {
//		dev_err(&pdev->dev, "Failed to allocate mem memory\n");
//		ret = -ENOMEM;
//		goto error1;
//	}
//	dev_info(&pdev->dev, "Allocated mem:\nVIRT: 0x%016llx\nPHYS: 0x%016llx\n", (u64)mem, mem_phys);

	uioinfo->name = "memmap";
	uioinfo->version = "0.1";
	uioinfo->irq = UIO_IRQ_NONE;
	uioinfo->open = uio_memmap_open;
	uioinfo->release = uio_memmap_release;

//	uioinfo->mem[0].name = "mem0";
//	uioinfo->mem[0].addr = mem_phys;
//	uioinfo->mem[0].internal_addr = mem;
//	uioinfo->mem[0].size = DYNAMIC_MAP_SIZE;
//	uioinfo->mem[0].memtype = UIO_MEM_PHYS;

	uioinfo->mem[0].name = "mem0";
	uioinfo->mem[0].addr = 0;
	uioinfo->mem[0].internal_addr = 0;
	uioinfo->mem[0].size = DYNAMIC_MAP_SIZE;
	uioinfo->mem[0].memtype = UIO_MEM_PHYS;

//	pm_runtime_enable(&pdev->dev);

	ret = uio_register_device(&pdev->dev, priv->uioinfo);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register UIO device\n");
//		pm_runtime_disable(&pdev->dev);
		goto error2;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "probe successful\n");

	return 0;

error2:
//	dma_free_coherent(&pdev->dev, DYNAMIC_MAP_SIZE, mem, mem_phys);
error1:
	of_reserved_mem_device_release(&pdev->dev);
error0:
	return ret;
}

static int uio_memmap_remove(struct platform_device *pdev)
{
	struct uio_memmap_platdata *priv = platform_get_drvdata(pdev);

	uio_unregister_device(priv->uioinfo);
//	pm_runtime_disable(&pdev->dev);
//	dma_free_coherent(&pdev->dev, DYNAMIC_MAP_SIZE, priv->uioinfo->mem[0].internal_addr, priv->uioinfo->mem[0].addr);
	of_reserved_mem_device_release(&pdev->dev);

	dev_info(&pdev->dev, "removed\n");
	return 0;
}


//static int uio_memmap_runtime_nop(struct device *dev)
//{
//	return 0;
//}

//static const struct dev_pm_ops uio_memmap_dev_pm_ops = {
//	.runtime_suspend = uio_memmap_runtime_nop,
//	.runtime_resume = uio_memmap_runtime_nop,
//};

static const struct of_device_id uio_memmap_of_match[] = {
	{ .compatible = "rj,uio-memmap", },
//	{ .compatible = "rj,uio-memtest", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, uio_memmap_of_match);

static struct platform_driver uio_memmap_driver = {
	.probe = uio_memmap_probe,
	.remove = uio_memmap_remove,
	.driver = {
		.name = "uio-memmap",
		.of_match_table = uio_memmap_of_match,
//		.pm = &uio_memmap_dev_pm_ops,
	},
};
module_platform_driver(uio_memmap_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Russell Joyce");
MODULE_DESCRIPTION("UIO-based Memory Test");
MODULE_VERSION("0.1");
