#include "userprog/tss.h"
#include <debug.h>
#include <stddef.h>
#include "userprog/gdt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

/*
 * Task-State Segment(TSS).
 *
 * TSS의 인스턴스는 x86-64 전용 구조체로, 프로세서에 내장된 멀티태스킹 지원
 * 형식인 "태스크"를 정의하는 데 사용된다. 하지만 이식성, 속도, 유연성 등의
 * 이유로 대부분의 x86-64 운영체제는 TSS를 거의 완전히 무시한다. 우리도 예외는
 * 아니다.
 *
 * 불행히도 TSS로만 할 수 있는 일이 하나 있다. 바로 유저 모드에서 발생한
 * 인터럽트에 대한 스택 전환이다. 유저 모드(ring 3)에서 인터럽트가 발생하면
 * 프로세서는 현재 TSS의 rsp0 멤버를 참조해 인터럽트 처리에 사용할 스택을
 * 결정한다. 따라서 우리는 TSS를 만들고 최소한 그 필드들을 초기화해야 하며,
 * 이 파일이 바로 그 일을 한다.
 *
 * 인터럽트가 인터럽트 게이트 또는 트랩 게이트를 통해 처리될 때
 * (우리가 처리하는 모든 인터럽트가 여기에 해당한다), x86-64 프로세서는
 * 다음과 같이 동작한다.
 *
 *    - 인터럽트에 의해 중단된 코드가 인터럽트 핸들러와 같은 ring에 있으면
 *      스택 전환은 일어나지 않는다. 이는 커널에서 실행 중일 때 발생한
 *      인터럽트의 경우다. 이 경우 TSS 내용은 중요하지 않다.
 *
 *    - 중단된 코드가 핸들러와 다른 ring에 있으면, 프로세서는 새로운 ring에
 * 대해 TSS에 지정된 스택으로 전환한다. 이는 유저 공간에서 발생한 인터럽트의
 *      경우다. 이미 사용 중인 스택으로 전환하면 손상이 생길 수 있으므로,
 *      사용 중이지 않은 스택으로 전환하는 것이 중요하다. 우리는 유저 공간에서
 *      실행 중일 때 현재 프로세스의 커널 스택은 사용 중이지 않다는 것을
 *      알고 있으므로 언제나 그것을 사용할 수 있다. 따라서 스케줄러가 스레드를
 *      전환할 때, TSS의 스택 포인터도 새 스레드의 커널 스택을 가리키도록
 *      변경된다. (호출 위치는 thread.c의 schedule 안이다.)
 */

/*
 * 커널 TSS.
 */
struct task_state *tss;

/*
 * 커널 TSS를 초기화한다.
 */
void
tss_init (void) {
	/*
	 * 우리의 TSS는 call gate나 task gate에서 절대 사용되지 않으므로,
	 * 실제로 참조되는 필드는 일부뿐이며, 따라서 그 필드들만 초기화한다.
	 */
	tss = palloc_get_page (PAL_ASSERT | PAL_ZERO);
	tss_update (thread_current ());
}

/*
 * 커널 TSS를 반환한다.
 */
struct task_state *
tss_get (void) {
	ASSERT (tss != NULL);
	return tss;
}

/*
 * TSS의 ring 0 스택 포인터가 스레드 스택의 끝을 가리키도록 설정한다.
 */
/*
 * 유저 모드 -> 커널 모드 전환 시 사용할 현재 스레드의 커널 스택 시작 주소
 */
void
tss_update (struct thread *next) {
	ASSERT (tss != NULL);
	tss->rsp0 = (uint64_t) next + PGSIZE;
}
