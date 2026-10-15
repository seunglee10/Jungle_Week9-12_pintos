#include <stdbool.h>
#include <stddef.h>

/*
 * 발표 스크린샷용:
 * 실제 커밋 이력에 남아 있던 "문제 코드"만 모은 파일
 *
 * 목표:
 * - 미구현/초안이 아니라 실제로 런타임 문제를 만들 수 있었던 코드만 보여준다.
 * - 슬라이드에서는 코드 스크린샷 + 왜 터졌는지 한 줄 설명으로 사용한다.
 */

typedef int tid_t;

struct intr_frame {
	struct {
		long rax;
	} R;
	long rdi;
};

struct child_status {
	int exit_status;
	bool exited;
	bool fork_success;
	int fork_sema;
	int wait_sema;
};

struct thread {
	void *pml4;
	struct child_status *self_status;
	void **fd_table;
};

struct file;

void exit (int status);
void thread_exit (void);
void check_address (const void *addr);
void *pml4_get_page (void *pml4, const void *addr);
void sema_up (int *sema);
void list_remove (void *elem);
void free (void *ptr);
struct thread *thread_current (void);
int process_exec (void *f_name);

/* ------------------------------------------------------------------------- */
/* 1. 유저 포인터를 그대로 커널 process_exec()에 넘겼던 코드                  */
/* 근거 커밋: 81c626e 이전                                                   */
/* https://github.com/Jungle-12-303/wk09_1/commit/81c626ebb1cd2c15c8e04bb7ad277140e21fb00f */
/* ------------------------------------------------------------------------- */

void
syscall_exec (struct intr_frame *f) {
	/*
	 * 문제:
	 * - check_address()는 시작 주소 한 번만 검사한다.
	 * - 그런데 커널은 그 뒤에 user pointer를 그대로 process_exec()로 넘긴다.
	 * - 문자열이 페이지 경계를 넘거나 중간에 invalid page가 있으면
	 *   커널이 그대로 잘못된 주소를 따라가며 페이지 폴트로 이어질 수 있다.
	 */
	check_address ((void *) f->R.rdi);
	f->R.rax = process_exec ((void *) f->R.rdi);
}

/* ------------------------------------------------------------------------- */
/* 2. current thread / pml4 가드 없이 주소를 검증하던 코드                    */
/* 근거 커밋: 2539c19 이전                                                   */
/* https://github.com/Jungle-12-303/wk09_1/commit/2539c19e4f0d5c7b4c82fabd7b60eaebfe98e33b */
/* ------------------------------------------------------------------------- */

void
buggy_check_address (const void *addr) {
	struct thread *curr = thread_current ();

	if (addr == NULL)
		exit (-1);

	if (!is_user_vaddr (addr))
		exit (-1);

	/*
	 * 문제:
	 * - curr == NULL 인 경우를 막지 않음
	 * - curr->pml4 == NULL 인 경우도 막지 않음
	 * - 결국 pml4_get_page(curr->pml4, ...) 자체가 잘못된 문맥에서 호출될 수 있음
	 */
	if (pml4_get_page (curr->pml4, addr) == NULL)
		exit (-1);

	if (curr->pml4 != NULL && pml4_get_page (curr->pml4, addr) == NULL)
		exit (-1);
}

/* ------------------------------------------------------------------------- */
/* 3. fork 실패 경로에서 self_status를 붙든 채 부모를 먼저 깨우던 코드         */
/* 근거 커밋: 8da296a 이전                                                   */
/* https://github.com/Jungle-12-303/wk09_1/commit/8da296a404a1410dff7ed61ce98f2fb5b15a04a8 */
/* ------------------------------------------------------------------------- */

void
buggy_fork_error_path (void) {
	struct thread *curr = thread_current ();

	if (curr->self_status != NULL) {
		/*
		 * 문제:
		 * - fork 실패를 기록하고 부모를 깨운다.
		 * - 부모는 깨어난 뒤 child_status를 list_remove/free 할 수 있다.
		 * - 그런데 자식은 curr->self_status를 그대로 붙잡고 thread_exit()로 간다.
		 * - 이후 exit 경로에서 self_status를 다시 건드리면 dangling pointer가 될 수 있다.
		 */
		curr->self_status->exit_status = -1;
		curr->self_status->exited = true;
		curr->self_status->fork_success = false;
		sema_up (&curr->self_status->fork_sema);
	}
	thread_exit ();
}

/* ------------------------------------------------------------------------- */
/* 발표용 핵심 문장                                                           */
/* ------------------------------------------------------------------------- */

/*
 * 1. 유저/커널 경계:
 *    유저 포인터를 그대로 커널 내부 함수에 넘기면 페이지 경계에서 바로 터질 수 있었다.
 *
 * 2. 주소 검증 문맥:
 *    current thread와 pml4가 보장되지 않은 상태에서 주소 검증을 수행하면
 *    검사 함수 자체가 패닉의 원인이 될 수 있었다.
 *
 * 3. 실패 경로 ownership:
 *    부모를 먼저 깨우는 순간, 자식이 붙든 상태 포인터는 dangling pointer가 될 수 있었다.
 */
