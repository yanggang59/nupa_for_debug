#ifndef __NUPA_H_
#define __NUPA_H_

#include "queue.h"

/**
* Note: This is the public header for both driver and app
*/

#define RESERVE_MEM_SIZE                                (0x100000000)
#define NUPA_BLOCK_SIZE                                 (4 * 1024 * 1024)
#define NUPA_BLOCK_SIZE_MASK                            (NUPA_BLOCK_SIZE -1)
#define NUPA_META_DATA_LEN                              (24 * (NUPA_BLOCK_SIZE))
#define NUPA_DATA_SIZE                                  (RESERVE_MEM_SIZE - NUPA_META_DATA_LEN)
#define CACHE_BLOCK_NUM                                 (NUPA_DATA_SIZE / NUPA_BLOCK_SIZE)
#define BITMAP_SIZE                                     ((CACHE_BLOCK_NUM + sizeof(unsigned long) -1 )/sizeof(unsigned long))
#define LOCAL_RAMDISK_TEST                              1
#if LOCAL_RAMDISK_TEST
#define NUPA_DISK_SIZE                                  (1024 * 1024)
#else
#define NUPA_DISK_SIZE                                  RESERVE_MEM_SIZE
#endif
#define INVALID_ADDR                                    ULONG_MAX


struct nupa_meta_info_header {
	unsigned long dirty_bit_map[BITMAP_SIZE];
	unsigned long vb[CACHE_BLOCK_NUM];
};

/**************************************************subq and comq*************************************************/

#define QUEUE_SIZE	      128
enum req_type { 
	REQ_READ = 1, 
	REQ_WRITE,  
};

struct nupa_queue_entry {
	unsigned long pb;
	enum req_type req; 
};


extern struct nupa_meta_info_header* g_meta_info_header;
extern struct queue *g_nupa_sub_queue;
extern struct queue *g_nupa_com_queue;


static void queue_assign_to(struct queue *qbase, int head, void *value)
{
	struct nupa_queue_entry* entry = (struct nupa_queue_entry*)((char*)qbase->entries + (head * sizeof(struct nupa_queue_entry)));
	struct nupa_queue_entry* _entry = (struct nupa_queue_entry*)value;
	entry->pb  = _entry->pb;
	entry->req = _entry->req;

	//memcpy((char*)qbase->entries + (head * sizeof(struct nupa_queue_entry)), entry, sizeof(struct nupa_queue_entry));
}

static void queue_assign_from(struct queue *qbase, int tail, void *value)
{
	struct nupa_queue_entry* entry = (struct nupa_queue_entry*)((char*)qbase->entries + (tail * sizeof(struct nupa_queue_entry)));
	struct nupa_queue_entry* _entry = (struct nupa_queue_entry*)value;
	_entry->pb  = entry->pb;
	_entry->req = entry->req;
	//memcpy(entry, (char*)qbase->entries + (tail * sizeof(struct nupa_queue_entry)), sizeof(struct nupa_queue_entry));
}


/**
*                                 meta_data               entries                      entries
*       ----------------------------------------------------------------------------------------------------------
*       |                             | header |           |      |      |      |           |      |      |
*       |         DATA                |        | sub queue |  x   |   x  |  ... | com queue |  x   |  x   |  ...  
*       |                             |        |           |      |      |      |           |      |      |
*       ----------------------------------------------------------------------------------------------------------
*/

static void nupa_meta_data_init(void* base_addr)
{
	g_meta_info_header = (struct nupa_meta_info_header*)((char*)base_addr + NUPA_DATA_SIZE);
#ifndef USER_APP
	memset(g_meta_info_header, 0, sizeof(struct nupa_meta_info_header) + 2 * sizeof(struct queue) + 2 * QUEUE_SIZE * sizeof(struct nupa_queue_entry));
	//memset(g_meta_info_header, 0, sizeof(struct nupa_meta_info_header));
	memset(g_meta_info_header->vb, 0xFF, sizeof(g_meta_info_header->vb));
#endif
	g_nupa_sub_queue = (struct queue *)((char*)g_meta_info_header + sizeof(struct nupa_meta_info_header));
	//memset(g_nupa_sub_queue, 0, sizeof(struct queue));
	g_nupa_sub_queue->size = QUEUE_SIZE;
	g_nupa_sub_queue->entries = (struct nupa_queue_entry*) ((char*)g_nupa_sub_queue + sizeof(struct queue));
	g_nupa_sub_queue->assign_to = queue_assign_to;
	g_nupa_sub_queue->assign_from = queue_assign_from;

	g_nupa_com_queue = (struct queue *)((char*)g_nupa_sub_queue + QUEUE_SIZE * sizeof(struct nupa_queue_entry));
	//memset(g_nupa_com_queue, 0, sizeof(struct queue));
	g_nupa_com_queue->size = QUEUE_SIZE;
	g_nupa_com_queue->entries = (struct nupa_queue_entry*)((char*)g_nupa_com_queue + sizeof(struct queue));
	g_nupa_com_queue->assign_to = queue_assign_to;
	g_nupa_com_queue->assign_from = queue_assign_from;
	return;
}

static bool is_vb_dirty(unsigned long vb, unsigned long* dirty_bit_map)
{
	unsigned long byte = vb / sizeof(unsigned long);
	unsigned long offset = vb % sizeof(unsigned long);
	unsigned value = dirty_bit_map[byte];
	return (value & (1 << offset));
}

static void set_vb_dirty(unsigned long vb, unsigned long* dirty_bit_map)
{
	unsigned long byte = vb / sizeof(unsigned long);
	unsigned long offset = vb % sizeof(unsigned long);
	dirty_bit_map[byte] |= 1 << offset;
}

#ifdef USER_APP
static void clr_vb_dirty(unsigned long vb, unsigned long* dirty_bit_map)
{
	unsigned long byte = vb / sizeof(unsigned long);
	unsigned long offset = vb % sizeof(unsigned long);
	dirty_bit_map[byte] &= ~(1 << offset);
}
#endif

#endif