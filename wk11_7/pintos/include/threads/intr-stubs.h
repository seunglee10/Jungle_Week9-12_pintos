#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/*
 * 인터럽트 스텁.
 *
 * 이는 intr-stubs.S 안에 있는 작은 코드 조각들로,
 * 가능한 256개의 x86 인터럽트 각각에 대해 하나씩 존재한다.
 * 각 스텁은 스택을 약간 조작한 뒤 intr_entry()로 점프한다.
 * 자세한 내용은 intr-stubs.S를 참고하라.
 *
 * 이 배열은 각 인터럽트 스텁의 진입점을 가리키므로,
 * intr_init()가 그것들을 쉽게 찾을 수 있다.
 */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* threads/intr-stubs.h */
