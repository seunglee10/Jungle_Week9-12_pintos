#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 인터럽트가 켜져 있는가, 꺼져 있는가?
 */
enum intr_level {
	/*
	 * 인터럽트가 비활성화됨.
	 */
	INTR_OFF,
	/*
	 * 인터럽트가 활성화됨.
	 */
	INTR_ON
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/*
 * 인터럽트 스택 프레임.
 */
struct gp_registers {
	uint64_t r15; /* 범용 레지스터, 함수 호출 뒤에도 보존되는 callee-saved 레지스터. */
	uint64_t r14; /* 범용 레지스터, 함수 호출 뒤에도 보존되는 callee-saved 레지스터. */
	uint64_t r13; /* 범용 레지스터, 함수 호출 뒤에도 보존되는 callee-saved 레지스터. */
	uint64_t r12; /* 범용 레지스터, 함수 호출 뒤에도 보존되는 callee-saved 레지스터. */
	uint64_t r11; /* 범용 임시 레지스터, syscall 진입 시 플래그 저장에도 쓰인다. */
	uint64_t r10; /* 범용 임시 레지스터, syscall 인자 전달에 쓰일 수 있다. */
	uint64_t r9;  /* 범용 레지스터, 함수/시스템 콜의 6번째 인자 전달에 쓰인다. */
	uint64_t r8;  /* 범용 레지스터, 함수/시스템 콜의 5번째 인자 전달에 쓰인다. */
	uint64_t rsi; /* Source index, 함수/시스템 콜의 2번째 인자 전달에 쓰인다. */
	uint64_t rdi; /* Destination index, 함수/시스템 콜의 1번째 인자 전달에 쓰인다. */
	uint64_t rbp; /* Base pointer, 현재 스택 프레임의 기준 주소로 주로 쓰인다. */
	uint64_t rdx; /* Data register, 함수/시스템 콜의 3번째 인자 전달에 쓰인다. */
	uint64_t rcx; /* Count register, 반복/시프트 명령이나 4번째 함수 인자에 쓰인다. */
	uint64_t rbx; /* Base register, 함수 호출 뒤에도 보존되는 callee-saved 레지스터. */
	uint64_t rax; /* Accumulator, 함수 반환값과 syscall 번호/반환값에 주로 쓰인다. */
} __attribute__((packed));

struct intr_frame {
	/*
	 * intr-stubs.S의 intr_entry가 푸시한 값들이다.
	 * 이는 인터럽트된 태스크의 저장된 레지스터들이다.
	 */
	struct gp_registers R;
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;
	uint16_t __pad3;
	uint32_t __pad4;
	/*
	 * intr-stubs.S의 intrNN_stub가 푸시한 값이다.
	 */
	/*
	 * 인터럽트 벡터 번호.
	 */
	uint64_t vec_no;
	/*
	 * 때때로 CPU가 푸시하고,
	 * 그렇지 않으면 일관성을 위해 intrNN_stub가 0으로 푸시한다.
	 * CPU는 이것을 `eip` 바로 아래에 두지만, 우리는 이를 여기로 옮긴다.
	 */
	uint64_t error_code;
	/*
	 * CPU가 푸시한 값들이다.
	 * 이는 인터럽트된 태스크의 저장된 레지스터들이다.
	 */
	uintptr_t rip; /* Instruction pointer, 인터럽트가 발생했을 때 실행 중이던 명령어 주소. */
	uint16_t cs;
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;
	uintptr_t rsp; /* Stack pointer, 인터럽트가 발생했을 때의 스택 최상단 주소. */
	uint16_t ss;
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
