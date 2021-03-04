#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;				//디스크에 존재하는 sector의 수
	unsigned int fat_start;					  //fat가 시작되는 sector의 번호
	unsigned int fat_sectors;         /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;      //fat_boot 부팅 정보
	unsigned int *fat;		   //FAT
	unsigned int fat_length; //데이터 영역의 섹터 수/SECTORS_PER_CLUSTER
	disk_sector_t data_start;//데이터 여역이 시작되는 지점의 sector 번호
	cluster_t last_clst;		 
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);
	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();  // fat_fs->fat_length 설정
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");
	
	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);
	
	//?---------start----------------
	ASSERT(fat_fs->fat_length > 2);
	fat_put(2, 0);
	for (int i = 3; i < fat_fs->fat_length; i++){
		fat_put(i, i-1);
	}
	fat_fs->last_clst = fat_fs->fat_length - 1;
	//?---------end----------------

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
			//? fat_sectors : 157 disk_size (filesys_disk) : 20160

	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

/* Initialize FAT file system. You have to initialize 
 * fat_length and data_start field of fat_fs. 
 * fat_length stores how many clusters in the filesystem 
 * and data_start stores in which sector we can start to
 * store files. You may want to exploit some values stored 
 * in fat_fs->bs. Also, you may want to initialize some 
 * other useful data in this function.*/
void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	struct fat_boot fb = fat_fs->bs;
	fat_fs->fat_length = (fb.total_sectors - fb.fat_start 
												- fb.fat_sectors)/SECTORS_PER_CLUSTER;
	fat_fs->data_start = fb.fat_start + fb.fat_sectors + 2*SECTORS_PER_CLUSTER;
	lock_init(&fat_fs->write_lock);
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/


/*Extend a chain by appending a cluster 
after the cluster specified in clst (cluster indexing number). 
If clst is equal to zero, then create a new chain. 
Return the cluster number of newly allocated cluster.*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	cluster_t alloc_clst = fat_fs->last_clst;
	if(alloc_clst == 0){//? 2 or 0?!
		PANIC("fat_create_chain - return 0??!");
		return 0;
	}
	fat_fs->last_clst = fat_get(alloc_clst);
	fat_put(alloc_clst, EOChain);
	if(clst)
		fat_put(clst, alloc_clst);
	return alloc_clst;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	if (pclst != 0){
		ASSERT(fat_get(pclst) == clst);
		fat_put(pclst, EOChain);
	}

	cluster_t origin_clst = clst;
	while(fat_get(clst) != EOChain){
		clst = fat_get(clst);
	}
	fat_put(clst, fat_fs->last_clst);
	fat_fs->last_clst = origin_clst;
}

/*Update FAT entry pointed by cluster number clst to val. 
Since each entry in FAT points the next cluster in a chain 
(if exist; otherwise EOChain), this could be used to update 
connectivity.*/

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	ASSERT(clst < fat_fs->fat_length);
	fat_fs->fat[clst] = val; 
}

/* Fetch a value in the FAT table. */
/* Return in which cluster number 
 * the given cluster clst points.*/
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	ASSERT(clst < fat_fs->fat_length);
	return fat_fs->fat[clst];
}

/* Translates a cluster number clst into 
 * the corresponding sector number and 
 * return the sector number.*/
/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	//(X - 2) x (클러스터 당 섹터 수) + (클러스터 2의 섹터 수)
	ASSERT(1 <= clst);
	return (clst-2)*SECTORS_PER_CLUSTER + fat_fs->data_start;
}

cluster_t
sector_to_cluster (disk_sector_t sector) {
	ASSERT(sector >= 0);
	return (sector - fat_fs->data_start) / SECTORS_PER_CLUSTER + 2;
}

disk_sector_t
get_sector_using_fat (cluster_t start, off_t pos){
	off_t pos_left = pos %(DISK_SECTOR_SIZE * SECTORS_PER_CLUSTER);
	off_t cluster_cnt = pos / (DISK_SECTOR_SIZE * SECTORS_PER_CLUSTER);
	
	for(int i = 0; i < cluster_cnt; i++){
		start = fat_get(start);
		if(start == EOChain){
			PANIC("Can't go further");
		}
	}
	disk_sector_t sector = cluster_to_sector(start);
	sector += pos_left / DISK_SECTOR_SIZE;
	return sector;
}