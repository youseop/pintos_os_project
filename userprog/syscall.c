#include "userprog/syscall.h"
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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

//c000008100000000
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
syscall_handler (struct intr_frame *f UNUSED) {
  // TODO: Your implementation goes here.
  /*
  get stack pointer from interrupt frame 
  get system call number from stack      
  switch (system call number){           
    case the number is halt:
      call halt function;
      break;
    case the number is exit:
      call exit function;
      break;
    …
    default
      call thread_exit function;

  */

  check_address(f->rsp,f->R.rdi);
  switch((f->R.rax)){
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(f->R.rdi);
      break;
    case SYS_FORK:
      memcpy(&thread_current()->if_,f,sizeof(struct intr_frame));
      f->R.rax = fork(f->R.rdi);
      break;
    case SYS_EXEC:
      f->R.rax = exec(f->R.rdi);
      break;
    case SYS_WAIT:
      f->R.rax = wait(f->R.rdi);
      break;
    case SYS_CREATE:
      /* 파일 이름과 크기에 해당하는 파일 생성 */
      /* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
      if (f->R.rdi){
        f->R.rax = filesys_create (f->R.rdi, f->R.rsi);
      }
      else{
        exit(-1);
      }
      break;
    case SYS_REMOVE:
      /* 파일 이름에 해당하는 파일을 제거 */
      /* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
      f->R.rax = filesys_remove (f->R.rdi);
      break;
    case SYS_OPEN:
      if(f->R.rdi)
        f->R.rax = open(f->R.rdi);
      else exit(-1);
      break;
    case SYS_FILESIZE:
      f->R.rax = filesize(f->R.rdi);
      break;
    case SYS_READ:
      f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
      break;
    case SYS_WRITE:
      f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
      break;
    case SYS_SEEK:
      seek(f->R.rdi, f->R.rsi);
      break;
    case SYS_TELL:
      f->R.rax = tell(f->R.rdi);
      break;
    case SYS_CLOSE:
      close(f->R.rdi);
      break;
    default:
      exit(f->R.rdi);
      break;
  }
}


/* 유저 스택에 저장된 인자값들을 커널로 저장 */
/* 인자가 저장된 위치가 유저영역인지 확인 */
void get_argument(void *rsp, int **arg , int count){
  rsp = (int64_t *)rsp + 2;
  for(int i=0;i<count;i++){
      arg[i] = rsp;
      rsp = (int64_t *)rsp + 1;
  }
}

int fork (const char *thread_name){
  tid_t child_tid = process_fork(thread_name,&thread_current()->tf);
  if(child_tid == -1)
    return -1;
  struct thread* child = get_child_process(child_tid); 

  if(child == NULL){
    return -1;
  }  
  printf("addr: %p\n",thread_current());
  sema_down(&child->fork_sema);

  if(child->exit_status == -1){
    return -1;
  }
  return child_tid;
}

/* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */
/* 파일 디스크립터를 이용하여 파일 객체 검색 */
/* 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에 저장 후
버퍼의 저장한 크기를 리턴 (input_getc() 이용) */
/* 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기만큼 저
장 후 읽은 바이트 수를 리턴 */
int read (int fd, void *buffer, unsigned size){
  lock_acquire(&filesys_lock);
  if(fd == 0){
    for(int i = 0; i<size; i++){
      ((char*)buffer)[i] = input_getc();
    }
    lock_release(&filesys_lock);
    return size;
  }
  else if(fd < 0){
    lock_release(&filesys_lock);
    return -1;
  }
  else if(fd > 2){
    struct file *f = process_get_file(fd);
    if(f == NULL){
      lock_release(&filesys_lock);
      return -1; 
    }
    size = file_read(f, buffer, size);
    lock_release(&filesys_lock);
    return size;
  }
  lock_release(&filesys_lock);
  exit(-1);  
}

int write (int fd, const void *buffer, unsigned size){
  lock_acquire(&filesys_lock);
  if(fd == 1){
    putbuf(buffer, size);
    lock_release(&filesys_lock);
    return size;
  } 
  else if(fd < 2){
    lock_release(&filesys_lock);
    return -1;
  }
  else{
    struct file* f = process_get_file(fd);
    size = file_write(f, buffer, size);
    lock_release(&filesys_lock);
    return size;
  }
}

void check_address(void *addr, int status)
{
  /* 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
  /* 잘못된 접근일 경우 프로세스 종료 */
  if( !is_user_vaddr(addr)){
    printf("[get_argument] isn't pointing to user addr\n");
    exit(status);
  }
}

void exit (int status) {
  struct thread * curr = thread_current();

  curr->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit ();
}


void halt (void){
  power_off();
}

int exec(const char*cmd_line){
  return process_exec(cmd_line);
}

int wait(tid_t tid){
  return process_wait(tid);
}

/* 파일을 open */
/* 해당 파일 객체에 파일 디스크립터 부여 */
/* 파일 디스크립터 리턴 */
/* 해당 파일이 존재하지 않으면 -1 리턴 */
int open(const char *file){
  struct file *f = filesys_open(file);
  if (f == NULL){
    return -1;
  }
  return process_add_file(f);
}

/* 파일 디스크립터를 이용하여 파일 객체 검색 */
/* 해당 파일의 길이를 리턴 */
/* 해당 파일이 존재하지 않으면 -1 리턴 */
int filesize (int fd){
  struct file *f = process_get_file(fd);
  if(f == NULL)
    return -1;
  return file_length(f);
}

/* 파일 디스크립터를 이용하여 파일 객체 검색 */
/* 해당 열린 파일의 위치(offset)를 position만큼 이동 */
void seek (int fd, unsigned position){
  struct file *f = process_get_file(fd);
  if(f == NULL){
    return -1;
  }
  file_seek(f, position);
}

/* 파일 디스크립터를 이용하여 파일 객체 검색 */
/* 해당 열린 파일의 위치를 반환 */
unsigned tell (int fd){
  struct file *f = process_get_file(fd);
  return file_tell(f);
}

/* 해당 파일 디스크립터에 해당하는 파일을 닫음 */
/* 파일 디스크립터 엔트리 초기화 */ 
void close(int fd){
  process_close_file(fd);
}