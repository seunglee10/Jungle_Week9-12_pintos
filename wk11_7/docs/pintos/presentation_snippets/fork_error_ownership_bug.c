#include <stdbool.h>

/*
 * fork 실패 경로에서 self_status ownership이 꼬였던 버그
 *
 * 실제 관련 커밋:
 * - 문제 코드 기준: 8da296a 이전
 * - 정리 코드 반영: 8da296a / 01a4ab2
 *
 * 발표용 스크린샷 목적의 더미 파일이다.
 * 컴파일 목적이 아니라 "무엇이 문제였고 어떻게 정리했는가"를 보여주기 위한 코드다.
 */

struct child_status {
	int exit_status;
	bool exited;
	bool fork_success;
	int fork_sema;
};

struct thread {
	struct child_status *self_status;
};

struct thread *thread_current (void);
void sema_up (int *sema);
void thread_exit (void);

/* ------------------------------------------------------------------------- */
/* BEFORE: 부모를 먼저 깨우고도 self_status를 계속 붙들고 있는 코드            */
/* ------------------------------------------------------------------------- */

void
fork_error_path (void) {
	struct thread *curr = thread_current ();

	if (curr->self_status != NULL) {
		curr->self_status->exit_status = -1;
		curr->self_status->exited = true;
		curr->self_status->fork_success = false;
		sema_up (&curr->self_status->fork_sema);
	}
	thread_exit ();
}

/*
 * 왜 문제였는가?
 *
 * 1. 자식이 fork 실패 사실을 기록한다.
 * 2. sema_up()으로 부모를 먼저 깨운다.
 * 3. 부모는 깨어난 뒤 child_status를 회수할 수 있다.
 * 4. 그런데 자식은 curr->self_status를 그대로 들고 thread_exit()로 간다.
 *
 * 즉, 부모가 먼저 회수하면 자식이 이미 해제된 상태 포인터를
 * 다시 만질 수 있는 dangling pointer 문제가 생긴다.
 */

/* ------------------------------------------------------------------------- */
/* AFTER: ownership을 끊고 부모를 깨우는 코드                                 */
/* ------------------------------------------------------------------------- */

void
fixed_fork_error_path (void) {
	struct thread *curr = thread_current ();

	if (curr->self_status != NULL) {
		struct child_status *cs = curr->self_status;

		cs->exit_status = -1;
		cs->exited = true;
		cs->fork_success = false;
		curr->self_status = NULL;
		sema_up (&cs->fork_sema);
	}
	thread_exit ();
}

/*
 * 어떻게 해결했는가?
 *
 * 1. curr->self_status를 지역 변수 cs로 먼저 분리한다.
 * 2. 부모를 깨우기 전에 curr->self_status = NULL로 ownership을 끊는다.
 * 3. 그 뒤 sema_up()으로 부모를 깨운다.
 *
 * 이렇게 하면 부모가 child_status를 회수하더라도,
 * 자식은 더 이상 curr->self_status를 통해 같은 포인터를 다시 참조하지 않는다.
 */
