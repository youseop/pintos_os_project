#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"


static char* STATUS[23] = {
	"SYS_HALT",
	"SYS_EXIT",
	"SYS_FORK",
	"SYS_EXEC",
	"SYS_WAIT",
	"SYS_CREATE",
	"SYS_REMOVE",
	"SYS_OPEN",
	"SYS_FILESIZE",
	"SYS_READ",
	"SYS_WRITE",
	"SYS_SEEK",
	"SYS_TELL",
	"SYS_CLOSE",
	"SYS_MMAP",
	"SYS_MUNMAP",
	"SYS_CHDIR",
	"SYS_MKDIR",
	"SYS_READDIR",
	"SYS_ISDIR",
	"SYS_INUMBER",
	"SYS_SYMLINK",
	"SYS_DUP2"
}; 
void syscall_entry (void);
void syscall_handler (struct intr_frame *f);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	//printf("\n\n # RSP # :: %p\n", f->R.rsi);
	uint64_t call_number = f->R.rax;
	// printf("\n\n<---------------- SYSCALL HEXDUMP at STATUS [%s] ------------------> \n", STATUS[f->R.rax]);
	// printf("rdi : %d\nrsi : %srdx : %d \n<-------------------------------------------------------------------->\n", f->R.rdi, f->R.rsi, f->R.rdx);
	// hex_dump(f->rsp, f->rsp, USER_STACK - f->rsp, true);
	if( !is_user_vaddr(f->rsp) ) call_number = SYS_BAD;

	uintptr_t *argv;
	void *rsp = f->rsp;
	//print_argument(argv, argc);
	//@ 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인
	switch(call_number) {
		//? rdi, rsi, rdx, r10, r8, r9
		case SYS_HALT :
			halt();
			break;
		case SYS_EXIT :
			exit(f->R.rdi);
			break;
		case SYS_FORK :
			memcpy(&thread_current()->_if, f, sizeof(struct intr_frame));
            f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC :
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT :
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE :
            if(f->R.rdi) { 
			    f->R.rax = create(f->R.rdi, f->R.rsi) ;
            }
            else exit(-1);
			break;
		case SYS_REMOVE :
			remove(f->R.rdi);
			break;
		case SYS_OPEN :
            if(f->R.rdi)
			    f->R.rax = open(f->R.rdi);
            else exit(-1);
			break;
		case SYS_FILESIZE :
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ :
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE :
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK :
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL :
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE :
			close(f->R.rdi);
			break;
		default :
			exit(f->R.rdi);
			break;
	}
}

_Bool
check_address(void *addr) {
	if(!is_user_vaddr(&addr)) ;
		//exit(-1);
}

void 
halt(void) {
	power_off();
}

void 
exit(int status) {
	thread_current()->exit_status = status;
	//thread_current()->parent_process->child_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

bool
create(const char *file, unsigned initial_size) {
	return filesys_create(file, initial_size);
}

int
write(int fd, const void *buffer, unsigned size) {
	lock_acquire(&filesys_lock);
	if(fd == 1) {
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	else if (fd < 2) {
		lock_release(&filesys_lock);
		return -1;
	}
	else {
		struct file* f = process_get_file(fd);
		size = file_write(f, buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
}

int
exec (const char* cmd_line) {
	//! 실패시 -1 , 성공 시 pid 리턴
	int pid = process_exec(cmd_line);
	return pid;
}

int
wait(int tid) {
	return process_wait(tid);
}

bool
remove(const char *file) {
	return filesys_remove(file);
}

int 
fork(const char *thread_name) {
	tid_t CHILD_tid = process_fork(thread_name, &thread_current()->tf);
	struct thread *child = get_child_process(CHILD_tid);
	if(child == NULL) return -1;
	sema_down(&child->fork_sema);
    if(child->exit_status == -1) return -1;
	if(thread_current()->tid == CHILD_tid)
		return 0;
	else
		return CHILD_tid;
}

int
open(const char *file) {
	struct thread *t = thread_current();
	struct file *f = filesys_open(file);
	if(f == NULL) return -1;
	return process_add_file(f);
}

int
filesize (int fd) {
	struct file *f = process_get_file(fd);
	if(f == NULL) return -1;
	return file_length(f);
}

int
read (int fd, void *buffer, unsigned size) {
	lock_acquire(&filesys_lock);
	if(fd < 0) {
		lock_release(&filesys_lock);
		return -1; //? -> -1
	}
	else if (fd == 0) {
		for(int i=0; i<size; ++i) {
			((char*)buffer)[i] = input_getc();
			// if(((char*)buffer)[i] == '\0') {
			// 	lock_release(&filesys_lock);
			// 	return i+1;
			// }
		}
		lock_release(&filesys_lock);
		return size;
	}
	else if(fd > 2) {
		//@ file read
		struct file *f = process_get_file(fd);
		if(f == NULL) {
			lock_release(&filesys_lock);
			return -1; //! -> size
		}
		size = file_read(f, buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	lock_release(&filesys_lock);
	exit(-1);
}

void
seek (int fd, unsigned position) {
	struct file* f = process_get_file(fd);
	if(f == NULL) return;
	file_seek(f, position); //? 의문이 있어.
}

unsigned
tell (int fd) {
	struct file* f = process_get_file(fd);
	return (unsigned)file_tell(f);
}

void
close (int fd) {
	process_close_file(fd);
}