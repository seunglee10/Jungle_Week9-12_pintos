#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

#ifdef VM
#include "vm/vm.h"
#endif

#include "threads/synch.h"
#include "threads/malloc.h"

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static struct child_info *find_child (tid_t child_tid);

static struct semaphore initd_sema;
static int initd_exit_status;
static tid_t initd_tid = TID_ERROR; /* Pintos 전체 종료 기준이 되는 최초 유저 프로세스 tid */
/* fork()는 자식 thread를 만들 때 부모의 정보도 같이 넘겨야 한다.
   부모 thread와 부모 intr_frame을 구조체 하나로 묶어서 넘긴다. */
struct fork_args {
	struct thread *parent;        /* fork를 호출한 부모 thread */
	struct intr_frame parent_if;  /* fork 순간 부모의 레지스터 상태 */
	struct semaphore fork_sema;   /* 자식 복사가 끝날 때까지 부모를 재우는 신호 */
	bool success;                 /* 자식 복사가 성공했는지 부모에게 알려주는 값 */
	struct child_info *child_info;/* 부모 wait와 자식 exit가 공유할 자식 상태 */
};
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	char *name_copy;
	tid_t tid;

	sema_init (&initd_sema, 0);

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL){
		return TID_ERROR;
	}
	strlcpy(fn_copy, file_name, PGSIZE);

	name_copy = palloc_get_page(0);
	if (name_copy == NULL){
		palloc_free_page(fn_copy);
		return TID_ERROR;
	}
	strlcpy(name_copy, file_name, PGSIZE);

	char *save_ptr;
	char *filename_only = strtok_r(name_copy, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(filename_only, PRI_DEFAULT, initd, fn_copy);
	
	palloc_free_page(name_copy);

	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	else
		initd_tid = tid; /* kernel main이 기다려야 하는 최초 유저 프로세스 tid를 저장 */

	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* 자식에게 넘길 fork 준비물 상자를 만든다.
	   여기에는 부모 thread, 부모 intr_frame, 동기화용 semaphore가 들어간다. */
	struct fork_args *args = palloc_get_page (PAL_ZERO);

	if (args == NULL)
		return TID_ERROR; /* 준비물 상자를 못 만들면 fork 실패 */

	/* 현재 실행 중인 thread가 fork를 호출한 부모다. */
	args->parent = thread_current ();
	struct thread *parent = args->parent;

	/* wait()가 찾을 수 있도록 부모와 자식이 공유할 child_info를 만든다. */
	struct child_info *child = malloc (sizeof *child);
	if (child == NULL) {
		palloc_free_page (args);
		return TID_ERROR;
	}

	child->pid = TID_ERROR;      /* thread_create() 성공 후 실제 tid로 채운다. */
	child->exit_status = -1;     /* 자식이 비정상 종료하거나 아직 상태가 없을 때 기본값 */
	child->exited = false;       /* 아직 자식은 종료되지 않았다. */
	child->waited = false;       /* 부모가 아직 이 자식을 wait하지 않았다. */
	sema_init (&child->wait_sema, 0); /* 부모가 wait에서 잠들 때 사용할 세마포어 */
	args->child_info = child;    /* 자식 __do_fork()에게도 같은 child_info를 넘긴다. */

	/* fork syscall 순간의 부모 레지스터 상태를 복사한다.
	   자식은 이 값을 기반으로 fork() 이후 지점부터 실행된다. */
	memcpy (&args->parent_if, if_, sizeof args->parent_if);

	/* 부모는 자식이 복사 성공/실패를 알려줄 때까지 기다려야 한다.
	   초기값 0이면 부모가 sema_down()을 호출했을 때 잠든다. */
	sema_init (&args->fork_sema, 0);
	args->success = false;

	/* 자식 thread를 만든다.
	   aux로 args를 넘겨 자식이 부모 정보와 semaphore에 접근하게 한다. */
	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, args);

	if (tid == TID_ERROR) {
		palloc_free_page (args); /* 자식 생성 실패 시 준비물 상자 회수 */
		free (child);            /* wait용 child_info도 더 이상 쓸 일이 없으므로 회수 */
		return TID_ERROR;
	}

	/* 깃북 요구사항:
	   부모는 자식이 복제에 성공했는지 실패했는지 알기 전까지 fork에서 반환하면 안 된다. */
	sema_down (&args->fork_sema);

	/* 자식이 기록한 성공 여부를 확인한다.
	   args는 아래에서 해제할 것이므로 success 값을 지역 변수로 빼둔다. */
	bool success = args->success;

	if (success) {
		/* wait(pid)가 찾을 수 있게 부모의 children 목록에 등록한다. */
		child->pid = tid;
		list_push_back (&parent->children, &child->elem);
	} else {
		/* 자식 복제 실패 시 부모 children에 올리지 않고 정보 객체를 정리한다. */
		free (child);
	}

	palloc_free_page (args); /* 부모가 fork 결과를 확인했으므로 준비물 상자 회수 */

	return success ? tid : TID_ERROR;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	/* 커널 주소공간은 모든 프로세스가 공유하므로 자식에게 따로 복사하지 않는다. */
	if (is_kernel_vaddr (va))
	{
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4. */
	/* 2. 부모 pml4에서 va가 실제로 연결된 페이지를 찾는다.
   va는 유저 프로그램이 보는 가상주소이고,
   parent_page는 커널이 실제 내용을 복사할 수 있는 페이지 주소다. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL)
	{
		return false; /* 부모 페이지를 못 찾으면 자식에게 복사할 수 없으므로 fork 실패 */
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	    TODO: NEWPAGE. */
	/* 3. 자식이 사용할 새 유저 페이지를 할당한다.
   부모 페이지를 그대로 공유하면 부모/자식 메모리가 서로 영향을 주므로,
   자식 전용 페이지를 새로 만들고 나중에 부모 내용을 복사해야 한다. */
	newpage = palloc_get_page (PAL_USER);

	if (newpage == NULL)
	{
		return false; /* 새 페이지를 못 만들면 주소공간 복사를 계속할 수 없으므로 fork 실패 */
	}
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	/* 4. 부모 페이지 내용을 자식 새 페이지에 그대로 복사한다.
    parent_page와 newpage는 둘 다 실제 페이지 내용에 접근 가능한 주소다. */
	memcpy (newpage, parent_page, PGSIZE);

	/* 부모 페이지의 쓰기 가능 여부를 확인한다.
	자식 페이지도 부모와 같은 권한을 가져야 하므로 이 값을 pml4_set_page()에 넘긴다. */
	writable = is_writable (pte);

	/* 5. 자식 pml4에 새 페이지를 연결한다.
	부모가 쓰던 가상주소 va를 자식도 똑같이 쓰게 하되,
	실제 연결되는 페이지는 자식 전용 newpage다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
	/* 6. 연결에 실패하면 방금 할당한 newpage를 회수한다.
	   회수하지 않으면 아무도 참조하지 않는 페이지가 남아서 메모리 누수가 된다. */
		palloc_free_page (newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;

	/* process_fork()에서 넘긴 aux를 fork_args 타입으로 다시 해석한다.
	   aux 안에는 부모 thread와 부모 intr_frame이 들어 있다. */
	struct fork_args *args = aux;

	/* 부모 thread.
	   뒤에서 duplicate_pte()가 parent->pml4를 보고 부모 주소공간을 복사한다. */
	struct thread *parent = args->parent;

	/* 현재 thread는 방금 생성된 자식 thread다. */
	struct thread *current = thread_current ();

	bool succ = true;

	/* 1. Read the cpu context to local stack.
	   부모가 fork syscall을 호출한 순간의 레지스터 상태를 자식의 intr_frame에 복사한다.
	   이 복사가 있어야 자식도 fork() 다음 줄부터 실행되는 것처럼 동작한다. */
	memcpy (&if_, &args->parent_if, sizeof (struct intr_frame));
	/* fork()는 부모와 자식에게 서로 다른 반환값을 준다.
	부모는 process_fork()의 반환값으로 자식 tid를 받고,
	자식은 여기서 rax를 0으로 바꿔 fork()가 0을 반환한 것처럼 만든다. */
	if_.R.rax = 0;
	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.
	 * 해당 투두 아래에 작성함 */
	/* fork()는 부모 프로세스의 열린 파일 목록도 자식에게 복사해야 한다.
	   부모와 자식이 같은 fd 번호를 쓸 수 있어야 fork-close 같은 테스트가 성립한다. */
	current->fd_table_size = parent->fd_table_size;
	current->fd_next = parent->fd_next;
	current->fd_table = NULL;

	/* 부모가 fd_table을 가지고 있으면, 자식도 같은 크기의 fd_table을 새로 만든다.
	   배열 자체는 새로 만들고, 각 파일 객체는 file_duplicate()로 복제한다. */
	if (parent->fd_table != NULL && parent->fd_table_size > 0) {
		current->fd_table = malloc (sizeof (struct file *) * current->fd_table_size);
		if (current->fd_table == NULL)
			goto error;

		/* 먼저 전부 NULL로 초기화한다.
		   중간에 실패했을 때 어디까지 닫아야 하는지 안전하게 만들기 위함이다. */
		for (int i = 0; i < current->fd_table_size; i++)
			current->fd_table[i] = NULL;

		/* fd 0, 1은 stdin/stdout 예약 번호라서 현재 구조에서는 파일 객체 복사 대상이 아니다.
		   실제 파일은 fd 2번부터 들어간다. */
		for (int i = 2; i < parent->fd_table_size; i++) {
			if (parent->fd_table[i] == NULL)
				continue;

			/* 부모 fd가 가리키던 파일을 자식 fd에도 복제해서 연결한다. */
			current->fd_table[i] = file_duplicate (parent->fd_table[i]);

			/* 복제 중 하나라도 실패하면 이미 복제한 파일들을 닫고 fork 실패로 처리한다. */
			if (current->fd_table[i] == NULL) {
				for (int j = 2; j < i; j++) {
					if (current->fd_table[j] != NULL)
						file_close (current->fd_table[j]);
				}

				free (current->fd_table);
				current->fd_table = NULL;
				current->fd_table_size = 0;
				current->fd_next = 2;
				goto error;
			}
		}
	}

	process_init ();

	/* 여기까지 왔다는 것은 자식 pml4 복사까지 성공했다는 뜻이다.
	부모가 sema_down()에서 기다리고 있으므로 성공 여부를 기록하고 깨운다. */
	if (succ) {
		/* 자식 종료 시 process_exit()가 부모의 child_info에 상태를 남길 수 있게 연결한다.
		   이 포인터는 부모의 children 리스트에 들어간 객체와 같은 객체다. */
		current->child_info = args->child_info;

		args->success = true;
		sema_up (&args->fork_sema);
		do_iret (&if_);
	}

error:
	/* 중간에 pml4_create()나 pml4_for_each()가 실패하면 여기로 온다.
	   실패도 부모에게 알려야 부모가 영원히 잠들지 않는다. */
	args->success = false;
	sema_up (&args->fork_sema);
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name, &_if);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	/* 현재 process_wait를 호출한 주체가 kernel main thread라면
	   pml4가 없는 커널 thread라면
	   기존 initd_sema 방식으로 최초 유저 프로그램을 기다린다. */
	if (thread_current()->pml4 == NULL) {
		sema_down(&initd_sema);
		return initd_exit_status;
	}

	/* 자식 여부 확인 */
	struct child_info *child = find_child (child_tid);
	/* pid에 해당하는 직접 자식이 아니면 wait 실패로 -1 반환 */
	if (child == NULL) {
		return -1;
	 }
	
	 /* 이미 wait 했으면 -1 반환 */
	if (child->waited) {
		return -1;
	}
	
	/* 같은 pid에 대해 wait를 두 번 성공하지 못하도록 표시 */
	child->waited = true;

	/* 자식이 아직 종료되지 않았으면 부모가 잠듦 */
	if(!child->exited) {
		sema_down(&child->wait_sema);
	}

	/* 자식 종료 후 남긴 상태값 저장 */
	int status = child->exit_status;

	/* children list에서 제거하고, 메모리 해제 */
	list_remove (&child->elem);
	free(child);
	
	return status;
}

/* 자식 찾기 */
static struct child_info *find_child (tid_t child_tid){
	struct thread *curr = thread_current();
	struct list_elem *e;

	/* curr->children [ child_info(pid=3).elem ] -> [ child_info(pid=7).elem ] */
	for (e = list_begin (&curr->children);
		e != list_end (&curr->children);
		e = list_next(e)) {
		struct child_info *child = list_entry (e, struct child_info, elem);

		if(child->pid == child_tid){
			return child;
			}
		}
	return NULL;
}
/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current();

	/* 유저 프로세스 종료 메시지 출력 */
	if (curr->pml4 != NULL) {
		printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	}

	/* initd_sema는 Pintos kernel main이 최초 유저 프로그램 종료를 기다릴 때만 쓴다. */
	if (curr->tid == initd_tid) {
		initd_exit_status = curr->exit_status;
		sema_up(&initd_sema);
	}

	/* 종료 중인 자식이면 부모가 wait()에서 읽을 종료 정보를 남긴다. */
	if (curr->child_info != NULL) {
		/* 현재 thread의 종료 상태를 부모가 읽을 child_info에 복사 */
		curr->child_info->exit_status = curr->exit_status;
		/* 종료 여부를 true로 변경 */
		curr->child_info->exited = true;
		/* 기다리고 있던 부모를 깨움 */
		sema_up (&curr->child_info->wait_sema);
	}

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

#define MAX_ARGS 64

static bool setup_stack (struct intr_frame *if_);
static bool setup_args (struct intr_frame *if_, char *cmdline);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	char *cp4_cmdline = NULL;
	char *cp4_filename = NULL;
	cp4_cmdline = palloc_get_page(0);
	cp4_filename = palloc_get_page(0);
	strlcpy(cp4_cmdline, file_name, PGSIZE);
	strlcpy(cp4_filename, file_name, PGSIZE);

	char *save_ptr;
	char *filename_only = strtok_r(cp4_filename, " ", &save_ptr);

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (filename_only);
	if (file == NULL) {
		printf ("load: %s: open failed\n", filename_only);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	if (!setup_args(if_, cp4_cmdline)){
		goto done;
	}

	t->fd_table_size = 64;
	t->fd_next = 2;
	t->fd_table = malloc (sizeof (struct file *) * t->fd_table_size);
	if (t->fd_table == NULL)
		goto done;
	for (int i = 0; i < t->fd_table_size; i++)
		t->fd_table[i] = NULL;
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	if (!success && t->fd_table != NULL) {
		free (t->fd_table);
		t->fd_table = NULL;
		t->fd_table_size = 0;
		t->fd_next = 2;
	}

	if (file != NULL)
		file_close (file);

	if (cp4_cmdline != NULL)
		palloc_free_page (cp4_cmdline);
	if (cp4_filename != NULL)
		palloc_free_page (cp4_filename);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

static bool
setup_args(struct intr_frame *if_, char *cmdline){
	char *argv[MAX_ARGS];
	char *arg_addr[MAX_ARGS];
	int argc = 0;

	char *token;
	char *save_ptr;

	for (token = strtok_r(cmdline, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)){
		argv[argc++] = token;
	}
	for (int i = argc - 1; i >= 0; i--){
		size_t len = strlen(argv[i]) + 1;
		if_->rsp -= len;
		memcpy((void *)if_->rsp, argv[i], len);
		arg_addr[i] = (char *)if_->rsp;
	}

	const size_t word_size = sizeof(char *);
	while (if_->rsp % word_size != 0){
		if_->rsp--;
		*(uint8_t *)if_->rsp = 0;
	}

	if_->rsp -= word_size;
	*(char **)if_->rsp = NULL;

	for (int i = argc - 1; i >= 0; i--){
		if_->rsp -= word_size;
		*(char **)if_->rsp = arg_addr[i];
	}

	char **argv_entry = (char **)if_->rsp;

	if_->rsp -= word_size;
	*(char **)if_->rsp = NULL;

	if_->R.rdi = (uint64_t)argc;
	if_->R.rsi = (uint64_t)argv_entry;

	return true;
}
 
/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
