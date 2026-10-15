#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/*
 * 가상 주소를 다루기 위한 함수와 매크로들.
 *
 * x86 하드웨어 페이지 테이블 전용 함수와 매크로는 pte.h를 참고하라.
 */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/*
 * 페이지 오프셋(0:12 비트).
 */
/*
 * 첫 번째 오프셋 비트의 인덱스.
 */
#define PGSHIFT 0
/*
 * 오프셋 비트 수.
 */
#define PGBITS  12
/*
 * 한 페이지의 바이트 수.
 */
#define PGSIZE  (1 << PGBITS)
/*
 * 페이지 오프셋 비트들(0:12).
 */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)

/*
 * 페이지 내부 오프셋.
 */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/*
 * 가장 가까운 페이지 경계까지 올림한다.
 */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/*
 * 가장 가까운 페이지 경계까지 내림한다.
 */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/*
 * 커널 가상 주소 시작점.
 */
#define KERN_BASE LOADER_KERN_BASE

/*
 * 유저 스택 시작점.
 */
#define USER_STACK 0x47480000

/*
 * VADDR가 유저 가상 주소이면 true를 반환한다.
 */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/*
 * VADDR가 커널 가상 주소이면 true를 반환한다.
 */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

/* 검사를 추가해야 한다. */
/*
 * 물리 주소 PADDR이 매핑되는 커널 가상 주소를 반환한다.
 */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/*
 * 커널 가상 주소 VADDR가 매핑되는 물리 주소를 반환한다.
 */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
