#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/pgtable.h>
#include <linux/hdreg.h>
#include <linux/uio_driver.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include "nupa_driver.h"
#include "nupa.h"

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

static int nupa_fops_open(struct block_device *bdev, fmode_t mode)
{
	printk("[Info] nupa_open \r\n");
	return 0;
}

static void nupa_fops_release(struct gendisk *disk, fmode_t mode)
{
	return ;
}

static int nupa_fops_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg)
{
    printk("[Info] ioctl cmd 0x%08x\n", cmd);
    return -ENOTTY;
}

static int nupa_fops_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 2;
	geo->cylinders = 32;
	geo->sectors = (NUPA_DISK_SIZE / geo->heads / geo->cylinders);
	return 0;
}

static void sync_read(unsigned long pb)
{
	struct nupa_queue_entry tmp_entry = {
		.pb = pb,
		.req = REQ_READ,
	};
	//push into sub queue
	while(qpush(g_nupa_sub_queue, &tmp_entry));
	
	//get result from com queue
	while((qpop(g_nupa_com_queue, &tmp_entry)) && (tmp_entry.pb == pb) && (tmp_entry.req == REQ_READ));
}


static void async_write(unsigned long pb)
{
	struct nupa_queue_entry tmp_entry = {
		.pb = pb,
		.req = REQ_WRITE,
	};
	//push into sub queue
#ifndef USER_APP
	printk("async_write In \r\n");
#endif
	while(qpush(g_nupa_sub_queue, &tmp_entry));
#ifndef USER_APP
	printk("async_write Out \r\n");
#endif
}

/**
* for read
* 1. if vb is invalid or vb is not equal to pb, read frome remote
*	 if vb is invalid ,do we have to wait until we read from remote? 
*    consider rebuild case, simplest solution is read from remote ,could be optimized later
* 2. if vb is valid , check dirty bit
*   (1) if clean, direct read
*   (2) if dirty, also direct read, coz local cache is always newest, no need to read from remote
*/
static void nupa_process_read(void* u_buf, void* k_buf, unsigned long start, struct bio_vec* bvec)
{
	unsigned long pb , vb, addr, size;
	unsigned long len  = bvec->bv_len;
	unsigned long offset = bvec->bv_offset;
	while(len) {
		pb = start / NUPA_BLOCK_SIZE;
        vb = pb % CACHE_BLOCK_NUM;
		addr = start & NUPA_BLOCK_SIZE_MASK;
		size = NUPA_BLOCK_SIZE - addr;
		if (len < size)
			size = len;

		if(g_meta_info_header->vb[vb] == INVALID_ADDR) {
			/**
			*  1. sync read from remote
			*  2. set pb
			*  3. do local copy
			*/
			sync_read(pb);
			g_meta_info_header->vb[vb] = pb;
		} else if(g_meta_info_header->vb[vb] != pb) {
			/**
			*  1. check dirty bit
			*  2. if dirty, wait till clean
			*  3. if clean, sync read from remote, set pb, then do local copy  
			*/
			while(is_vb_dirty(vb, g_meta_info_header->dirty_bit_map));
			sync_read(pb);
			g_meta_info_header->vb[vb] = pb;
		}else if(g_meta_info_header->vb[vb] == pb) {
			/**
			* doesn't matter if it's dirty or not, coz local is always newer then remote
			* just do local copy
			*/
			printk("[Read] local cache hit\r\n");
		}

		memcpy(u_buf + offset, k_buf + addr, size);

		start += size;
		offset += size;
		k_buf += size;
		u_buf += size;
        len -= size;
	}
}


/**
* for write
* 1. if vb is INVALID or vb is not equal to pb, direct write
* 2. if vb is equal to pb, check dirty bit
*    (1) if clean, direct write
*    (2) if dirty, wait till dirty bit is clean, then write  
*/
static void nupa_process_write(void* u_buf, void* k_buf, unsigned long start, struct bio_vec* bvec)
{
	unsigned long pb , vb, addr, size;
	unsigned long len  = bvec->bv_len;
	unsigned long offset = bvec->bv_offset;
	printk("[Info] nupa_process_write In , start = %lu, len = %lu , offset = %lu\r\n",start, len, offset);
	while(len) {
		pb = start / NUPA_BLOCK_SIZE;
        vb = pb % CACHE_BLOCK_NUM;
		addr = start & NUPA_BLOCK_SIZE_MASK;
		size = NUPA_BLOCK_SIZE - addr;
		if (len < size)
			size = len;
		
		printk("[Info] nupa_process_write pb = %lu \r\n", pb);
        if((g_meta_info_header->vb[vb] == INVALID_ADDR)) {
			/** This vb is new 
			* 1. write to local ,set pb and set dirty bit 
			* 2. async write to remote
			*/
			printk("[Info] newly written \r\n");
			memcpy(k_buf + addr, u_buf + offset, size);
			printk("[Info] mem copy done  \r\n");
			set_vb_dirty(vb, g_meta_info_header->dirty_bit_map);
			printk("[Info] mark dirty done  \r\n");
			g_meta_info_header->vb[vb] = pb;
			printk("[Info] replace cache done  \r\n");
			async_write(pb);
			printk("[Info] async write done  \r\n");
		} else if(g_meta_info_header->vb[vb] != pb) {
			/**  
			* 1. check dirty bit
			* 2. if clean, direct write
			* 3. if dirty, wait till dirty bit is clean, then read from remote
			* 4. write to local , set pb and set dirty bit
			* 5. async write to remote
			*/
			// if vb is dirty, wait till app flush dirty block into remote and clean dirty bit
			printk("[Info] cache miss \r\n");
			while(is_vb_dirty(vb, g_meta_info_header->dirty_bit_map)); 
			memcpy(k_buf + addr, u_buf + offset, size);
			set_vb_dirty(vb, g_meta_info_header->dirty_bit_map);
			g_meta_info_header->vb[vb] = pb;
			async_write(pb);
		} else if(g_meta_info_header->vb[vb] == pb) {
			/** 
			* 1. check dirty bit
			* 2. if clean, direct write
			* 3. if dirty, wait till dirty bit is clean
			* 4. write to local , set pb and set dirty bit
			* 5. async write to remote
			*/
			printk("[Info] rewrite \r\n");
			while(is_vb_dirty(vb, g_meta_info_header->dirty_bit_map)); 
			memcpy(k_buf + addr, u_buf + offset, len);
			set_vb_dirty(vb, g_meta_info_header->dirty_bit_map);
			async_write(pb);
		} 
        start += size;
		offset += size;
		k_buf += size;
		u_buf += size;
        len -= size;
	}
	printk("[Info] nupa_process_write Out \r\n");
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,2,0)
static blk_qc_t nupa_fops_submit_bio(struct bio *bio)
#else
static void nupa_fops_submit_bio(struct bio *bio)
#endif
{
	int offset;
	unsigned long start;
	struct bio_vec bvec;
	struct bvec_iter iter;

	start = bio->bi_iter.bi_sector << SECTOR_SHIFT;

	bio_for_each_segment(bvec, bio, iter) {
		unsigned int len = bvec.bv_len;
		struct page* page = bvec.bv_page;
		void* dst = kmap(page);
		offset = bvec.bv_offset;
		if (bio_op(bio) == REQ_OP_READ) {
#if CONFIG_SUPPORT_WAF
			nupa_process_read(dst, g_nupa_dev->nupa_buf, start, &bvec);
#else
			memcpy(dst + offset, g_nupa_dev->nupa_buf + start, len);
#endif
		} else if (bio_op(bio) == REQ_OP_WRITE) {
#if CONFIG_SUPPORT_WAF
			nupa_process_write(dst, g_nupa_dev->nupa_buf, start, &bvec);
#else
			memcpy(g_nupa_dev->nupa_buf + start, dst + offset, len);
#endif
		}
		start += len;
		kunmap(page);
	}
	bio_endio(bio);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,2,0)
	return BLK_QC_T_NONE;
#else
	return ;
#endif
}


static const struct block_device_operations nupa_fops = {
	.owner = THIS_MODULE,
	.open = nupa_fops_open,
	.release = nupa_fops_release,
	.ioctl = nupa_fops_ioctl,
	.getgeo = nupa_fops_getgeo,
	.submit_bio = nupa_fops_submit_bio,
};

static int nupa_register_disk(struct nupa_dev* nupa_dev)
{
	struct gendisk *disk;
	int err;

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (IS_ERR(disk))
		return PTR_ERR(disk);

	disk->major = g_nupa_dev->major;
	disk->first_minor = 0;
	disk->minors = 1;
	disk->fops = &nupa_fops;

	sprintf(disk->disk_name, "nupa");

	nupa_dev->nupa_gendisk = disk;
	err = add_disk(disk);
	if (err)
		put_disk(disk);
	set_capacity(disk, (NUPA_DISK_SIZE >> 9));
	return err;
}

#if DEBUG
static void simple_buf_test(void* buf, long size)
{
	if(size > NUPA_DISK_SIZE)
		size = NUPA_DISK_SIZE;
	memset(buf, 'A', size);
}
#endif

static int __init blockdev_init(void)
{
	int ret;
	g_nupa_dev = kmalloc(sizeof(struct nupa_dev), GFP_KERNEL);
	if(!g_nupa_dev) {
		printk("[Error] Create nupa_dev failed \r\n");
		goto out;
	}
	g_nupa_dev->major = register_blkdev(0, DEVICE_NAME);
#if LOCAL_RAMDISK_TEST
	g_nupa_dev->nupa_buf = kmalloc(NUPA_DISK_SIZE, GFP_KERNEL);
#else
	g_nupa_dev->nupa_buf = ioremap(RESERVE_MEM_START, NUPA_DISK_SIZE);
#endif
#if DEBUG
	simple_buf_test(g_nupa_dev->nupa_buf, 1024 * 1024);
	printk("[Info] nupa_buf = %p \r\n", g_nupa_dev->nupa_buf);
#endif	
    ret = nupa_register_disk(g_nupa_dev);
    if (ret) {
		goto out;
	}
#if CONFIG_SUPPORT_WAF
	nupa_meta_data_init(g_nupa_dev->nupa_buf);
#endif
	nupa_uio_init();
	return 0;
out:
	return ret;
}

static void __exit blockdev_exit(void)
{
	unregister_blkdev(g_nupa_dev->major, DEVICE_NAME);
    del_gendisk(g_nupa_dev->nupa_gendisk);
    put_disk(g_nupa_dev->nupa_gendisk);
#if LOCAL_RAMDISK_TEST
	kfree(g_nupa_dev->nupa_buf);
#else
	iounmap(g_nupa_dev->nupa_buf);
#endif
	kfree(g_nupa_dev);
	nupa_uio_exit();
	return;
}

module_init(blockdev_init);
module_exit(blockdev_exit);
MODULE_LICENSE("GPL");
