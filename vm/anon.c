/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/*
* 익명 페이지에 대한 작업을 제공합니다 (vm_type = VM_ANON).
*/

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

struct swap_table {
	struct lock lock;              /* Mutual exclusion. */
	struct bitmap *bit_map;       /* Bitmap of free pages. */ 
};
static struct swap_table swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	swap_disk = disk_get(1, 1);
	disk_sector_t swap_disk_size = disk_size(swap_disk); //? 8064
	uint64_t bit_cnt = swap_disk_size/8;                 //? 1008
	swap_table.bit_map = bitmap_create(bit_cnt);
	ASSERT(swap_table.bit_map);

	lock_init(&swap_table.lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_idx = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	
	size_t swap_idx = anon_page->swap_idx;
	size_t bitmap_idx = swap_idx / 8;
	int PGSIZE_d8 = PGSIZE/8;
	for(int i = 0; i < 8; i++){
		disk_read(swap_disk, swap_idx+i, page->frame->kva + PGSIZE_d8 * i);
	}
	bitmap_set_multiple(swap_table.bit_map, bitmap_idx, 1, false);
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	lock_acquire (&swap_table.lock);
	size_t swap_idx = 8 * bitmap_scan_and_flip (swap_table.bit_map, 0, 1, false);
	anon_page->swap_idx = swap_idx;
	lock_release (&swap_table.lock);
	int PGSIZE_d8 = PGSIZE/8;
	for(int i = 0; i < 8; i++){
		disk_write(swap_disk, swap_idx+i, page->frame->kva + PGSIZE_d8 * i);
	}
	
	pml4_clear_page(thread_current()->pml4, page->va);
	palloc_free_page(page->frame->kva);

	free(page->frame);
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame){
		free(page->frame);
	}
	free(page->anon.aux);
}
