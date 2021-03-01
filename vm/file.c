/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

/*
* 파일 기반 페이지에 대한 작업을 제공합니다 (vm_type = VM_FILE).
*/

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	ASSERT(page);
	
	struct file_page *file_page UNUSED = &page->file;
	struct load_args_tmp* args = page->file.aux;
	uint8_t* upage = page->va;

	file_seek(args->file, args->ofs);
	if (file_read (args->file, kva, args->read_bytes) != (int) args->read_bytes) {
		palloc_free_page (kva);
		PANIC("file_Backd swap in failed!");
		return false;
	}

	memset(kva + args->read_bytes, 0, args->zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	ASSERT(page);

	struct file_page *file_page UNUSED = &page->file;
	struct load_args_tmp* aux = page->file.aux;

	if (pml4_is_dirty(thread_current()->pml4,page->va)){
		file_seek(aux->file, aux->ofs);
		file_write(aux->file, page->va, aux->read_bytes);
	} 

	pml4_clear_page(thread_current()->pml4, page->va);
	
	page->frame = NULL;

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct load_args_tmp* aux = page->file.aux;
	if (pml4_is_dirty(thread_current()->pml4,page->va)){
		file_seek(aux->file, aux->ofs);
		file_write(aux->file, page->va, aux->read_bytes);
	} 
	memset(page->va,0 ,aux->read_bytes);

	// if(page->file.aux->save_addr == page->va){
	// 	file_close(page->file.aux->file);
	// }
	//?여기서 file_close하면 문제가 생긴다.
	//?file과 관련된 마지막 page structure인지 알 수 있어야 하는데,
	//?여기서는 현재 구현방식으로는 불가능!
	//?mummap으로 옮겨주었다.

	if (page->frame)
		free(page->frame);
	free(page->file.aux);  
}

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	uint8_t* kpage = (page->frame)->kva;
	uint8_t* upage = page->va;
	struct load_args_tmp* args = page->uninit.aux;
	
	file_seek(args->file, args->ofs);
	if (file_read (args->file, kpage, args->read_bytes) != (int) args->read_bytes) {
		palloc_free_page (kpage);
		return false;
	}
	memset(kpage + args->read_bytes, 0, args->zero_bytes);
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void* save_addr = addr;
	ASSERT(pg_round_down(addr) == addr);
	struct file* new_file = file_reopen(file);
	off_t file_size = file_length(new_file);
	
	uint32_t read_bytes = file_size > length ? length : file_size;
	uint32_t zero_bytes = pg_round_up(read_bytes) - read_bytes;
	
	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct load_args_tmp* args = (struct load_args_tmp*)malloc(sizeof(struct load_args_tmp));
		args->file = new_file;
		args->ofs = offset;
		args->read_bytes = page_read_bytes;
		args->zero_bytes = page_zero_bytes;
		args->save_addr = addr;
		args->read_bytes_sum = read_bytes;
		if (!vm_alloc_page_with_initializer (VM_FILE, addr, 
					writable, lazy_load_segment, args))
			PANIC("vm_alloc_failed\n");

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes; //? important!!
	}
	return save_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct supplemental_page_table* spt = &thread_current()->spt;
	struct hash *h = &(spt->hash_table);
	struct page* new_page = spt_find_page(spt, addr);

	struct file* file = new_page->file.aux->file;

	int page_cnt = (((uint64_t) ((new_page->file.aux->read_bytes)) + PGSIZE - 1) & ~PGMASK)/PGSIZE;
	for(int i = page_cnt-1; i >= 0; i--){
		struct page* page = spt_find_page(spt, addr+i*PGSIZE);
		hash_delete(h, &page->hash_elem);
		supplemental_page_table_destructor (&page->hash_elem, h->aux);
	}
	file_close(file);
}