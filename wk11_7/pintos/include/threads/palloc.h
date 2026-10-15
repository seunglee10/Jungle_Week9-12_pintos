#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stdint.h>
#include <stddef.h>

/*
 * 페이지를 어떻게 할당할지 나타내는 플래그.
 */
enum palloc_flags {
	/*
	 * 실패 시 패닉을 일으킨다.
	 */
	PAL_ASSERT = 001,
	/*
	 * 페이지 내용을 0으로 채운다.
	 */
	PAL_ZERO = 002,
	/*
	 * 유저 페이지이다.
	 */
	PAL_USER = 004
};

/*
 * 유저 풀에 넣을 최대 페이지 수.
 */
extern size_t user_page_limit;

uint64_t palloc_init (void);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

#endif /* threads/palloc.h */
