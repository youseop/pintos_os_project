#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {

	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();
	/*do_format에서 curr_dir을 지정하게 되면, -f flag가 포함되지 않았을 때는
	 *curr_dir이 지정되지 않는다. do_format에서 curr_dir을 지정하더라도, 
	 *다른 testcase는 통과할 수 있겠지만, persistence testcase에서는 
	 *-f로 format을 시키지 않기 때문에 do_format에 curr_dir을 입력하는 부분이
	 *들어가 있다면, curr_dir을 찾지 못하는 문제가 발생한다.	*/
	struct dir* curr_dir = dir_open (inode_open (cluster_to_sector (ROOT_DIR_CLUSTER)));
	thread_current()->curr_dir = curr_dir;
	dir_add (curr_dir, ".", inode_get_inumber(dir_get_inode(curr_dir)));
	dir_add (curr_dir, "..", inode_get_inumber(dir_get_inode(curr_dir)));
	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

#ifdef EFILESYS
/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	char path_name[NAME_MAX + 1];
	char file_name[NAME_MAX + 1];

	int cpy_len = NAME_MAX > strlen(name) ? strlen(name) : NAME_MAX;
	memcpy(path_name,name,cpy_len+1);
	struct dir * dir = parse_path(path_name, file_name);
	bool success = (dir != NULL
			&& (inode_sector = cluster_to_sector(fat_create_chain(0)))
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (dir, file_name, inode_sector));

	if (!success && inode_sector != 0)
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	dir_close (dir);
	return success;
}

#else

bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);
	return success;
}
#endif

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {

	if (!strcmp(name, "/"))
		return dir_open_root();

	char path_name[NAME_MAX + 1];
	char file_name[NAME_MAX + 1];
	
	memcpy(path_name,name,strlen(name)+1);
	struct dir * dir = parse_path(path_name, file_name);

	struct inode *inode = NULL;
	if (dir != NULL)
		dir_lookup (dir, file_name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	if (!strcmp(name, "/"))
		return false;

	char path_name[NAME_MAX + 1];
	char file_name[NAME_MAX + 1];
	
	memcpy(path_name,name,strlen(name)+1);
	struct dir * dir = parse_path(path_name, file_name);
	struct inode *inode = NULL;
	bool success = (dir != NULL && dir_remove (dir, file_name));
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	if (!dir_create (cluster_to_sector(ROOT_DIR_CLUSTER), 2))
		PANIC ("root directory creation failed");
	fat_close ();

	//?.. .은 여기에 위치해야할수도...?
	// struct dir* curr_dir = dir_open (inode_open (cluster_to_sector (ROOT_DIR_CLUSTER)));
	// thread_current()->curr_dir = curr_dir;
	// dir_add (curr_dir, ".", inode_get_inumber(dir_get_inode(curr_dir)));
	// dir_add (curr_dir, "..", inode_get_inumber(dir_get_inode(curr_dir)));
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif
}

struct dir* 
parse_path (char *path_name, char *file_name) {
	struct dir *dir;
	if (path_name == NULL || strlen(path_name) == 0 || file_name == NULL)
		return NULL;
	if (path_name[0] == '/') {
		dir = dir_open_root();
	}
	else{
		dir = dir_reopen(thread_current()->curr_dir);
	}

	char *token, *nextToken, *savePtr;
	token = strtok_r (path_name, "/", &savePtr);
	nextToken = strtok_r (NULL, "/", &savePtr);
	struct inode *inode = NULL;

	while (token && nextToken) {
		dir_lookup(dir, token, &inode);
		if (inode == NULL || inode_is_file(inode)){
			return NULL;
		}
		dir_close(dir);
		dir = dir_open(inode);
		
		token = nextToken;
		nextToken = strtok_r (NULL, "/", &savePtr);
	}

	memcpy(file_name, token, strlen(token)+1);
	return dir;
}