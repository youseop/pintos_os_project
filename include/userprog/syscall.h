#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/off_t.h"
#include <stddef.h>
#include "vm/vm.h"

typedef int tid_t;

void syscall_init (void);

void get_argument(void *rsp, int **arg , int count);
tid_t fork (const char *thread_name);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void check_address(void *addr, int status);
void exit (int status);
void halt (void);
int exec(const char*cmd_line);
int wait(tid_t tid);
int open(const char *file);
int filesize (int fd);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close(int fd);
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);
bool isdir (int fd);
bool mkdir (const char *dir);
bool chdir (const char *dir);
int inumber (int fd);
bool readdir (int fd, char *name);
int symlink (const char *target, const char *linkpath);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
