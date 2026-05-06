#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "userprog/process.h"
#include "intrinsic.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "devices/input.h"

static void is_crazy_user_buffer(const void *buffer, size_t size);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
}
/* fd_table 확장 */
static bool
fd_table_expand (struct thread *t) {
	int old_size = t->fd_table_size;
	int new_size = old_size * 2;
	struct file **new_table = malloc (sizeof (struct file *) * new_size);

	if (new_table == NULL)
		return false;

	/* 기존 테이블 값 새 테이블로 복사 */
	for (int i = 0; i < old_size; i++)
		new_table[i] = t->fd_table[i];

	/* 새로 확장한 칸 NULL로 만듦 */
	for (int i = old_size; i < new_size; i++)
		new_table[i] = NULL;

	/* 기존 fd table 배열 반납 */
	free (t->fd_table);
	t->fd_table = new_table;
	t->fd_table_size = new_size;
	return true;
}
/* fd_next부터 빈 슬롯을 찾아 파일을 등록하고, 배정된 fd 반환.
   빈 슬롯이 없으면 fd_table을 확장한 뒤 다시 찾음  */
static int
fd_allocate (struct file *f) {
	struct thread *t = thread_current ();

	/* 열린 파일이 없거나, fd_table 없으면 실패 */
	if (f == NULL || t->fd_table == NULL)
		return -1;

	/* fd_next부터 빈 칸 찾기 */
	for (int i = t->fd_next; i < t->fd_table_size; i++) {
		if (t->fd_table[i] == NULL) {
			t->fd_table[i] = f;
			t->fd_next = i + 1;
			return i;
		}
	}

	if (!fd_table_expand (t))
		return -1;

	/* fd_table 확장 후 fd_next부터 돌면서 빈 fd 찾기 */
	for (int i = t->fd_next; i < t->fd_table_size; i++) {
		if (t->fd_table[i] == NULL) {
			t->fd_table[i] = f;
			t->fd_next = i + 1;
			return i;
		}
	}

	return -1;
}

/* fd 번호로 실제 struct file * 찾기 */
static struct file *
fd_table_get_file (int fd) {
	struct thread *t = thread_current ();

	if (fd < 2 || t->fd_table == NULL || fd >= t->fd_table_size)
		return NULL;

	return t->fd_table[fd];
}

/* fd를 닫기 */
static void
fd_close (int fd) {
	struct thread *t = thread_current ();

	if (fd < 2 || t->fd_table == NULL || fd >= t->fd_table_size)
		return;

	if (t->fd_table[fd] == NULL)
		return;

	file_close (t->fd_table[fd]);
	t->fd_table[fd] = NULL;

	/* fd_next보다 더 작은 fd가 비었으면 다음 open 때 그 번호를 재사용 */
	if (fd < t->fd_next)
		t->fd_next = fd;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	uint64_t arg0 = f->R.rdi;
	uint64_t arg1 = f->R.rsi;
	uint64_t arg2 = f->R.rdx;
	// TODO: Your implementation goes here.
	switch (f->R.rax){
		case SYS_OPEN: {
			const char *name = (const char *) arg0;
			struct file *opened = filesys_open (name);
			f->R.rax = fd_allocate (opened);
			if (f->R.rax == -1 && opened != NULL)
				file_close (opened);
			break;
		}
		
		case SYS_READ: {
			int fd = (int) arg0;
			void *buffer = (void *) arg1;
			unsigned size = (unsigned) arg2;
			/* 0바이트 읽기는 아무것도 하지 않고 0 반환 */
			if (size == 0) {
				f->R.rax = 0;
				break;
			}

			/* fd == 0 일 때 (stdin) 키보드 입력 읽기 */
			if (fd == 0) {
				is_crazy_user_buffer(buffer, size);
				uint8_t *buf = (uint8_t *) buffer;
				for (unsigned i = 0; i < size; i++)
					buf[i] = input_getc();
				f->R.rax = size;
			/* fd ==1 일 때 (stdout) 지금 읽기니까 이땐 무조건 -1 반환 */
			} else if (fd == 1) {
				f->R.rax = -1;
			/* fd >=2 일 때 open()으로 연 일반 파일이라고 보고 처리 */
			} else if (fd >= 2) {
				struct file *read_file = fd_table_get_file(fd);
				if (read_file == NULL) {
					f->R.rax = -1;
				} else {
					is_crazy_user_buffer(buffer, size);
					f->R.rax = file_read(read_file, buffer, size);
				}

			} else {
				f->R.rax = -1;
			}
			break;
		}

		case SYS_CLOSE:
			fd_close ((int) arg0);
			break;

		case SYS_WRITE:
			if ((arg0 == STDOUT_FILENO)){
				is_crazy_user_buffer((const void *) arg1, (size_t) arg2);
				putbuf((char *) arg1, (size_t) arg2);
				f->R.rax = arg2;
			}else{
				f->R.rax = -1;
			}
			break;

		case SYS_FORK:
			f->R.rax = process_fork ((const char *) arg0, f);
			break;

		case SYS_WAIT:
			f->R.rax = process_wait ((tid_t) arg0);
			break;

		case SYS_EXIT:
			curtain_call ((int) arg0);
			// thread_current()->exit_status = (int)f->R.rdi;
			// thread_exit();
			break;

		default:
			printf ("system call!\n");
			thread_exit ();
			break;
		}
	} 

void
curtain_call (int status) {
	thread_current ()->exit_status = status;
	thread_exit ();
	NOT_REACHED ();
}

static void
is_crazy_user_address (const void *u_addr) {
	if (u_addr == NULL || !is_user_vaddr (u_addr) ||
		pml4_get_page (thread_current ()->pml4, u_addr) == NULL) {
		thread_current ()->exit_status = -1;
		thread_exit ();
	}
}

static void
is_crazy_user_buffer (const void *buffer, size_t size) {
	const uint8_t *start = buffer;
	if (size == 0)
		return;

	for (size_t i = 0; i < size; i++)
		is_crazy_user_address (start + i);
}

