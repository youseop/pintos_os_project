#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"
#include "threads/mmu.h"
#include <hash.h>

struct page;
enum vm_type;

struct file_page {
	void* padding; 
	enum vm_type type;
	struct load_args_tmp *aux;
};

struct load_args_tmp{
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  void *save_addr;
  size_t read_bytes_sum;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
