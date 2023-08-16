#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include "nupa_driver.h"
#include "nupa.h"

#define NUPA_MINOR_CNT         (8) /* Move this down when adding a new minor */



static struct nupa_dev* g_nupa_dev;
struct nupa_meta_info_header* g_meta_info_header;
struct queue *g_nupa_sub_queue;
struct queue *g_nupa_com_queue;

struct uio_info nupa_uio_info = {
    .name = "nupa_uio",
    .version = "1.0",
    .irq = UIO_IRQ_NONE,
};

static int nupa_uio_probe(struct platform_device *pdev) {
  struct device *dev = &pdev->dev;
  nupa_uio_info.mem[0].name = "area1";
  nupa_uio_info.mem[0].addr = RESERVE_MEM_START;
  nupa_uio_info.mem[0].memtype = UIO_MEM_PHYS;
  nupa_uio_info.mem[0].size = NUPA_DISK_SIZE;
  return uio_register_device(dev, &nupa_uio_info);
}

static int nupa_uio_remove(struct platform_device *pdev) 
{
	printk("[Info] nupa_uio_remove\r\n");
	uio_unregister_device(&nupa_uio_info);
	return 0;
}
static struct platform_device *nupa_uio_device;
static struct platform_driver nupa_uio_driver = {
    .driver =
        {
            .name = "nupa_uio",
        },
    .probe = nupa_uio_probe,
    .remove = nupa_uio_remove,
};

static int nupa_uio_init(void) 
{
  nupa_uio_device = platform_device_register_simple("nupa_uio", -1, NULL, 0);
  return platform_driver_register(&nupa_uio_driver);
}

static void nupa_uio_exit(void) 
{
  platform_device_unregister(nupa_uio_device);
  platform_driver_unregister(&nupa_uio_driver);
}

static int nupa_open(struct block_device *bdev, fmode_t mode)
{
    printk("[Info] nupa_open \r\n");
	return 0;
}

static void nupa_release(struct gendisk *disk, fmode_t mode)
{
    printk("[Info] nupa_release \r\n");
}

static const struct block_device_operations nupa_fops =
{
	.owner		= THIS_MODULE,
	.open		= nupa_open,
	.release	= nupa_release,
};

static struct kobject *nupa_find(dev_t dev, int *part, void *data)
{
	*part = 0;
	return get_disk_and_module(g_nupa_dev->nupa_gendisk);
}

static struct request_queue *nupa_queue;


static void __nupa_make_request(struct bio *bio)
{
    int offset;
	long start;
	struct bio_vec bvec;
	struct bvec_iter iter;

	start = bio->bi_iter.bi_sector << SECTOR_SHIFT;

	bio_for_each_segment(bvec, bio, iter) {
		unsigned int len = bvec.bv_len;
		struct page* page = bvec.bv_page;
		void* dst = kmap(page);
		offset = bvec.bv_offset;
		if (bio_op(bio) == REQ_OP_READ) {
			memcpy(dst + offset, g_nupa_dev->nupa_buf + start, len);
		} else if (bio_op(bio) == REQ_OP_WRITE) {
			memcpy(g_nupa_dev->nupa_buf + start, dst + offset, len);
		}
		start += len;
		kunmap(page);
	}
	bio_endio(bio);
}

static blk_qc_t nupa_make_request(struct request_queue *queue, struct bio *bio)
{
	__nupa_make_request(bio);
	return BLK_QC_T_NONE;
}

static int __init nupa_init(void)
{
    int ret;
    g_nupa_dev = kmalloc(sizeof(struct nupa_dev), GFP_KERNEL);
	if(!g_nupa_dev) {
		printk("[Error] Create nupa_dev failed \r\n");
        ret = -1;
		goto out;
	}
    g_nupa_dev->major = register_blkdev(0, DEVICE_NAME);
#if LOCAL_RAMDISK_TEST
	g_nupa_dev->nupa_buf = kmalloc(NUPA_DISK_SIZE, GFP_KERNEL);
#else
	g_nupa_dev->nupa_buf = ioremap(RESERVE_MEM_START, NUPA_DISK_SIZE);
#endif
    ret = -ENOMEM;
    g_nupa_dev->nupa_gendisk = alloc_disk(1);
    if (!g_nupa_dev->nupa_gendisk)
		goto out_disk;
    nupa_queue = blk_alloc_queue(GFP_KERNEL);
    if (IS_ERR(nupa_queue)) {
        ret = PTR_ERR(nupa_queue);
        nupa_queue = NULL;
		goto out_queue;
    }
    blk_queue_make_request(nupa_queue, nupa_make_request);
    g_nupa_dev->nupa_gendisk->major = g_nupa_dev->major;
    g_nupa_dev->nupa_gendisk->first_minor = 0;
    g_nupa_dev->nupa_gendisk->fops = &nupa_fops;
    sprintf(g_nupa_dev->nupa_gendisk->disk_name, "nupa");
    g_nupa_dev->nupa_gendisk->queue = nupa_queue;
    add_disk(g_nupa_dev->nupa_gendisk);
    blk_register_region(MKDEV(g_nupa_dev->major, 0), 0, THIS_MODULE, nupa_find, NULL, NULL);
	set_capacity(g_nupa_dev->nupa_gendisk, NUPA_DISK_SIZE >> 9);
    nupa_uio_init();
    return 0;

out_queue:
    put_disk(g_nupa_dev->nupa_gendisk);
out_disk:
    unregister_blkdev(g_nupa_dev->major, DEVICE_NAME);
out:   
    return ret;
}

static void __exit nupa_exit(void)
{
    blk_unregister_region(MKDEV(g_nupa_dev->major, 0), NUPA_MINOR_CNT);
    unregister_blkdev(g_nupa_dev->major, DEVICE_NAME);
    del_gendisk(g_nupa_dev->nupa_gendisk);
    put_disk(g_nupa_dev->nupa_gendisk);
    blk_cleanup_queue(nupa_queue);
#if LOCAL_RAMDISK_TEST
	kfree(g_nupa_dev->nupa_buf);
#else
	iounmap(g_nupa_dev->nupa_buf);
#endif
    kfree(g_nupa_dev);
    nupa_uio_exit();
    return;
} 

module_init(nupa_init);
module_exit(nupa_exit);
MODULE_LICENSE("GPL");
