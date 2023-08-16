#ifndef __NUPA_DRIVER_H_
#define __NUPA_DRIVER_H_


#define DEBUG                                           0
#define DEVICE_NAME                                     "nupa"
#define RESERVE_MEM_START                               (0x100000000)

#define SECTORS_PER_PAGE_SHIFT	                        (PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	                            (1 << SECTORS_PER_PAGE_SHIFT)


#define CONFIG_SUPPORT_WAF                              1

struct nupa_dev {
	struct device dev; 
	struct gendisk *nupa_gendisk;
	void* nupa_buf;
	int major;
	long virtual_capacity;
};

#endif