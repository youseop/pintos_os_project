/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "threads/palloc.h"
#include <hash.h>
#include "threads/mmu.h"
#include <stdio.h>
#include "userprog/process.h"

/*
* 	가상 메모리에 대한 일반 인터페이스를 제공합니다. 
* 	헤더 파일에서 가상 메모리 시스템이 지원해야하는 
* 	다른 vm_type (VM_UNINIT, VM_ANON, VM_FILE, VM_PAGE_CACHE)에 대한 
* 	정의와 설명을 볼 수 있습니다 (지금은 VM_PAGE_CACHE 무시, 프로젝트 4 용). 
* 	여기에서 보충 페이지 표를 구현할 수도 있습니다 (아래 참조).
*/

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void)

 {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page* page = (struct page*)malloc(sizeof(struct page));
		ASSERT(page);
		
		bool (*initializer)(struct page *, enum vm_type, void *);
		switch(VM_TYPE(type)){
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;			
				break;
			default:
				PANIC("###### vm_alloc_page_with_initializer [unvalid type] ######");
				break;
		}

		uninit_new(page, upage, init, type, aux, initializer);

		page->writable = writable;
		page->vm_type = type;

		/* TODO: Insert the page into the spt. */
		if(spt_insert_page(spt, page))
			return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. 
Find struct page that corresponds to 
va(* Address in terms of user space *) 
from the given supplemental page table. If fail, return NULL.*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	return page_lookup(pg_round_down(va));
}

/* Insert PAGE into spt with validation. 
Insert struct page into the given supplemental page table. 
This function should checks that the virtual address 
does not exist in the given supplemental page table.*/
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	//?This function should checks that the virtual address does not exist in the given supplemental page table.
	if(hash_insert(&spt->hash_table, &page->hash_elem) == NULL)
		return true;
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, //?this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	void* p = palloc_get_page(PAL_USER);
	if(p == NULL)
		PANIC("todo");
	
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	if (frame == NULL) 
		PANIC("failed to allocate frame");
	
	frame->kva = p;
	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	
	struct page *page = spt_find_page(spt,addr);
	if(page)
		return vm_do_claim_page (page);
	else
		return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	struct page *page = spt_find_page(&thread_current()->spt,va);
	ASSERT(page);
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	bool writable = page->writable;
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	int check = pml4_set_page(thread_current()->pml4, page->va, frame->kva, writable);
	
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table 

Initializes the supplemental page table. 
You may choose the data structure to use for the supplemental page table. 
The function is called when a new process starts (in initd of userprog/process.c) 
and when a process isF being forked (in __do_fork of userprog/process.c).*/
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash_table, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	bool success = true;
	struct hash *h = &src->hash_table;
	struct hash_iterator i;
	hash_first (&i, h);

	while (hash_next (&i))
	{
		//?get page from src
		struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);
		enum vm_type type = page->operations->type;

		//?copy args
		if (VM_TYPE(type) == VM_UNINIT){    //? VM_UNINIT
			struct load_args* args = (struct load_args*)malloc(sizeof(struct load_args));
			memcpy(args, page->uninit.aux, sizeof(struct load_args)); //?
			ASSERT(page->vm_type == VM_UNINIT);
			success = vm_alloc_page_with_initializer(page->vm_type, page->va,
					page->writable, page->uninit.init, args);
		}
		else if ((VM_TYPE(type) == VM_ANON)){
			if(page->vm_type & VM_STACK){		  //? VM_ANON | VM_STACK
				ASSERT(page->vm_type == VM_ANON | VM_STACK);
				success = vm_alloc_page(VM_ANON|VM_STACK, page->va, 1);
				vm_claim_page(page->va);
				struct page *new_page = spt_find_page (&thread_current()->spt, page->va);
				memcpy(new_page->va, page->frame->kva, PGSIZE);
			}
			else{        
				struct load_args* args = (struct load_args*)malloc(sizeof(struct load_args));
				memcpy(args, page->uninit.aux, sizeof(struct load_args)); //?
				ASSERT(page->vm_type == VM_ANON);//? VM_ANON
				success = vm_alloc_page_with_initializer(page->vm_type, page->va,
					page->writable, page->uninit.init, args);
				vm_claim_page(page->va);
				
				struct page *new_page = spt_find_page (&thread_current()->spt, page->va);
				memcpy(new_page->va, page->frame->kva, PGSIZE);
			}
		}
		ASSERT(success);
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}


/* Computes and returns the hash value for hash element E, given
 * auxiliary data AUX. */
uint64_t page_hash (const struct hash_elem *e, void *aux){
	const struct page* p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

/* Compares the value of two hash elements A and B, given
 * auxiliary data AUX.  Returns true if A is less than B, or
 * false if A is greater than or equal to B. */
bool page_less (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux){
	struct page* page_a = hash_entry(a, struct page, hash_elem);
	struct page* page_b = hash_entry(b, struct page, hash_elem);
	return page_a->va < page_b->va;
}

/* Returns the page containing the given virtual address, 
or a null pointer if no such page exists. */
struct page *
page_lookup (const void *address) {
  struct page p;
  struct hash_elem *e;

  p.va = address;
  e = hash_find (&thread_current()->spt.hash_table, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}
