/* page_cache.c: 페이지 캐시(버퍼 캐시) 구현. */

#include "vm/vm.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

/* 이 구조체는 수정하지 말 것. */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

/* 파일 VM 초기화 함수. */
void
pagecache_init (void) {
	/* TODO: page_cache_kworkerd를 사용해 페이지 캐시 워커 데몬 생성. */
}

/* 페이지 캐시 초기화. */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러 설정. */
	page->operations = &page_cache_op;

}

/* Swap in 메커니즘을 활용한 read-ahead 구현. */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* Swap out 메커니즘을 활용한 write-back 구현. */
static bool
page_cache_writeback (struct page *page) {
}

/* 페이지 캐시 파괴. */
static void
page_cache_destroy (struct page *page) {
}

/* 페이지 캐시용 워커 스레드. */
static void
page_cache_kworkerd (void *aux) {
}
