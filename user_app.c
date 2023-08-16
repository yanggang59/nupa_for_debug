#include <stdio.h>  
#include <fcntl.h>  
#include <stdlib.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <errno.h> 
#include <string.h>
#include <stdbool.h>

#define USER_APP
#include "nupa.h" 
  
#define UIO_DEV           "/dev/uio0"  
#define UIO_ADDR          "/sys/class/uio/uio0/maps/map0/addr"  
#define UIO_SIZE          "/sys/class/uio/uio0/maps/map0/size"
#define STORAGE_FILE      "./test.img"
#define DEBUG             1
  
static char uio_addr_buf[30], uio_size_buf[30];

struct nupa_meta_info_header* g_meta_info_header;
struct queue *g_nupa_sub_queue;
struct queue *g_nupa_com_queue;

static void* g_data_buf;

#define PRINT printf
static void print_buf(char* buf, int size)
{
	int i ,j;
  PRINT("print size = %d \r\n", size);
	PRINT("**********************************************************************\r\n");
    PRINT("   ");
	for(i = 0; i < 16; i++)
		PRINT("%4X",i);
    PRINT("\n======================================================================");
	for(j = 0; j < size; j++) {
		if(j % 16 == 0)
			PRINT("\n%4X||",j);
		PRINT("%4X",buf[j]);
	}
	PRINT("\n**********************************************************************\r\n");
}


static void user_process_write(struct nupa_queue_entry* cur_entry, int fd)
{
    unsigned long pb = cur_entry->pb;
    unsigned long vb = pb % CACHE_BLOCK_NUM;
    off_t offset = pb * NUPA_BLOCK_SIZE;
    void* buf = g_data_buf + vb * NUPA_BLOCK_SIZE;
    lseek(fd, offset, SEEK_SET);
    write(fd, buf, NUPA_BLOCK_SIZE);
    struct nupa_queue_entry tmp_entry = {
		.pb = pb,
		.req = REQ_WRITE,
	};
	//push into com queue
	while(qpush(g_nupa_com_queue, &tmp_entry));
    /**
    * Note: remember to clean dirty bit of the vb
    */
    clr_vb_dirty(vb, g_meta_info_header->dirty_bit_map);
}

static void user_process_read(struct nupa_queue_entry* cur_entry, int fd)
{
    unsigned long pb = cur_entry->pb;
    unsigned long vb = pb % CACHE_BLOCK_NUM;
    off_t offset = pb * NUPA_BLOCK_SIZE;
    void* buf = g_data_buf + vb * NUPA_BLOCK_SIZE;
    lseek(fd, offset, SEEK_SET);
    read(fd, buf, NUPA_BLOCK_SIZE);
    struct nupa_queue_entry tmp_entry = {
		.pb = pb,
		.req = REQ_READ,
	};
	//push into com queue
	while(qpush(g_nupa_com_queue, &tmp_entry));
}
  
int main(void)  
{  
  int uio_fd, addr_fd, size_fd, storage_fd;  
  long uio_size;  
  void* uio_addr, *access_address;
  off_t offset = 0; 
  struct nupa_queue_entry cur_entry; 
   
  uio_fd = open(UIO_DEV, /*O_RDONLY*/O_RDWR);  
  addr_fd = open(UIO_ADDR, O_RDONLY);  
  size_fd = open(UIO_SIZE, O_RDONLY);
  storage_fd = open(STORAGE_FILE, O_RDWR);  
  if( addr_fd < 0 || size_fd < 0 || uio_fd < 0 || storage_fd < 0) {  
       fprintf(stderr, "open: %s\n", strerror(errno));  
       exit(-1);  
  }  
  read(addr_fd, uio_addr_buf, sizeof(uio_addr_buf));  
  read(size_fd, uio_size_buf, sizeof(uio_size_buf));  
  uio_addr = (void*)strtoul(uio_addr_buf, NULL, 0);  
  uio_size = (long)strtol(uio_size_buf, NULL, 0);

  printf("uiofd = %d \r\n", uio_fd);
  printf(" uio_addr_buf = %s\n uio_size_buf = %s\n uio_addr = %p\n uio_size = 0x%lX\n", uio_addr_buf, uio_size_buf, uio_addr, uio_size);

  g_data_buf = access_address = mmap(NULL, NUPA_DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);  
  if ( access_address == (void*) -1) {  
      fprintf(stderr, "mmap: %s\n", strerror(errno));  
      exit(-1);  
  }  
  printf("The device address %p (lenth 0x%lX)\n"  
         "can be accessed over\n"  
         "logical address %p\n", uio_addr, uio_size, access_address); 
#if DEBUG
  print_buf(access_address, 4096);
#endif

  nupa_meta_data_init(access_address);



  while(1) {

    /**
     *   Fetch a req form subq
    */
    //sleep(1);
    while(qpop(g_nupa_sub_queue, &cur_entry));
    switch (cur_entry.req) {
        case REQ_WRITE:
            printf("[DEBUG] handle write req : %lu \r\n", cur_entry.pb);
            user_process_write(&cur_entry, storage_fd);
            break;
        case REQ_READ:
            printf("[DEBUG] handle read req : %lu \r\n", cur_entry.pb);
            user_process_read(&cur_entry, storage_fd);
            break;
        default:
            printf("[Error] current only support Read / Write\r\n");
            break;
    }
  }

  printf("user app exit \r\n");

  /**
  * For UIO
  */
  close(uio_fd);
  close(addr_fd);
  close(size_fd);
  close(storage_fd);
  return 0;  
}  