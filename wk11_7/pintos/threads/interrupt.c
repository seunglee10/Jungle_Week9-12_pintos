#include "threads/interrupt.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/gdt.h"
#endif

/*
 * x86_64 인터럽트의 개수.
 */
#define INTR_CNT 256

/*
 * FUNCTION을 호출하는 게이트를 생성한다.
 *
 * 이 게이트는 descriptor privilege level인 DPL을 가지며,
 * 이는 프로세서가 DPL 또는 그보다 더 낮은 번호의 ring에 있을 때
 * 의도적으로 호출될 수 있음을 의미한다. 실제로는 DPL==3이면 유저 모드가
 * 이 게이트를 호출할 수 있고, DPL==0이면 그런 호출을 막는다.
 * 유저 모드에서 발생한 fault와 exception은 여전히 DPL==0인 게이트를 호출하게 된다.
 *
 * TYPE은 14(인터럽트 게이트) 또는 15(트랩 게이트) 중 하나여야 한다.
 * 차이점은 인터럽트 게이트에 들어가면 인터럽트가 비활성화되지만,
 * 트랩 게이트에 들어가면 그렇지 않다는 점이다.
 * 자세한 내용은 [IA32-v3a] 5.12.1.2절
 * "Flag Usage By Exception- or Interrupt-Handler Procedure"를 참고하라.
 */

struct gate {
	/* 세그먼트 안 오프셋의 하위 16비트. */
	unsigned off_15_0 : 16;
	/* 세그먼트 셀렉터. */
	unsigned ss : 16;
	/* 인자 개수. 인터럽트/트랩 게이트는 0이다. */
	unsigned ist : 3;
	/* 예약됨(0이어야 한다). */
	unsigned rsv1 : 5;
	/* 타입(STS_{TG,IG32,TG32}). */
	unsigned type : 4;
	/* 반드시 0이어야 한다(system). */
	unsigned s : 1;
	/* 디스크립터 권한 단계. */
	unsigned dpl : 2;
	/* 존재 여부. */
	unsigned p : 1;
	/* 세그먼트 안 오프셋의 상위 비트들. */
	unsigned off_31_16 : 16;
	uint32_t off_32_63;
	uint32_t rsv2;
};

/*
 * 인터럽트 디스크립터 테이블(IDT).
 * 형식은 CPU에 의해 고정된다.
 * [IA32-v3a]의 5.10절 "Interrupt Descriptor Table (IDT)",
 * 5.11절 "IDT Descriptors", 5.12.1.2절
 * "Flag Usage By Exception- or Interrupt-Handler Procedure"를 참고하라.
 */
static struct gate idt[INTR_CNT];

static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};


#define make_gate(g, function, d, t) \
{ \
	ASSERT ((function) != NULL); \
	ASSERT ((d) >= 0 && (d) <= 3); \
	ASSERT ((t) >= 0 && (t) <= 15); \
	*(g) = (struct gate) { \
		.off_15_0 = (uint64_t) (function) & 0xffff, \
		.ss = SEL_KCSEG, \
		.ist = 0, \
		.rsv1 = 0, \
		.type = (t), \
		.s = 0, \
		.dpl = (d), \
		.p = 1, \
		.off_31_16 = ((uint64_t) (function) >> 16) & 0xffff, \
		.off_32_63 = ((uint64_t) (function) >> 32) & 0xffffffff, \
		.rsv2 = 0, \
	}; \
}

/*
 * 주어진 DPL로 FUNCTION을 호출하는 인터럽트 게이트를 생성한다.
 */
#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)

/*
 * 주어진 DPL로 FUNCTION을 호출하는 트랩 게이트를 생성한다.
 */
#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)



/*
 * 각 인터럽트에 대한 인터럽트 핸들러 함수들.
 */
static intr_handler_func *intr_handlers[INTR_CNT];

/*
 * 디버깅을 위한 각 인터럽트의 이름들.
 */
static const char *intr_names[INTR_CNT];

/*
 * 외부 인터럽트는 타이머처럼 CPU 바깥 장치가 만들어 내는 인터럽트다.
 * 외부 인터럽트는 인터럽트가 꺼진 상태에서 실행되므로 중첩되지도 않고,
 * 선점되지도 않는다. 외부 인터럽트 핸들러는 sleep할 수도 없다.
 * 다만 인터럽트가 반환되기 직전에 새 프로세스를 스케줄해 달라고 요청하기 위해
 * intr_yield_on_return()을 호출할 수는 있다.
 */
/*
 * 현재 외부 인터럽트를 처리 중인가?
 */
static bool in_external_intr;
/*
 * 인터럽트 반환 시 양보해야 하는가?
 */
static bool yield_on_return;

/*
 * 프로그래머블 인터럽트 컨트롤러 보조 함수들.
 */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/*
 * 인터럽트 핸들러들.
 */
void intr_handler (struct intr_frame *args);

/*
 * 현재 인터럽트 상태를 반환한다.
 */
enum intr_level
intr_get_level (void) {
	uint64_t flags;

	/*
	 * flags 레지스터를 프로세서 스택에 push한 뒤,
	 * 스택에서 값을 꺼내 `flags`에 넣는다.
	 * [IA32-v2b]의 "PUSHF", "POP"과 [IA32-v3a] 5.8.1절
	 * "Masking Maskable Hardware Interrupts"를 참고하라.
	 */
	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/*
 * LEVEL에 따라 인터럽트를 활성화하거나 비활성화하고,
 * 이전 인터럽트 상태를 반환한다.
 */
enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/*
 * 인터럽트를 활성화하고 이전 인터럽트 상태를 반환한다.
 */
enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	/*
	 * 인터럽트 플래그를 설정해 인터럽트를 활성화한다.
	 * [IA32-v2b]의 "STI"와 [IA32-v3a] 5.8.1절
	 * "Masking Maskable Hardware Interrupts"를 참고하라.
	 */
	asm volatile ("sti");

	return old_level;
}

/*
 * 인터럽트를 비활성화하고 이전 인터럽트 상태를 반환한다.
 */
enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	/*
	 * 인터럽트 플래그를 지워 인터럽트를 비활성화한다.
	 * [IA32-v2b]의 "CLI"와 [IA32-v3a] 5.8.1절
	 * "Masking Maskable Hardware Interrupts"를 참고하라.
	 */
	asm volatile ("cli" : : : "memory");

	return old_level;
}

/*
 * 인터럽트 시스템을 초기화한다.
 */
void
intr_init (void) {
	int i;

	/*
	 * 인터럽트 컨트롤러를 초기화한다.
	 */
	pic_init ();

	/*
	 * IDT를 초기화한다.
	 */
	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG
	/*
	 * TSS를 로드한다.
	 */
	ltr (SEL_TSS);
#endif

	/*
	 * IDT 레지스터를 로드한다.
	 */
	lidt(&idt_desc);

	/*
	 * intr_names를 초기화한다.
	 */
	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/*
 * 인터럽트 VEC_NO가 descriptor privilege level DPL로 HANDLER를 호출하도록
 * 등록한다. 디버깅 목적으로 인터럽트 이름을 NAME으로 붙인다.
 * 인터럽트 핸들러는 인터럽트 상태를 LEVEL로 설정한 채 호출된다.
 */
static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name) {
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON) {
		make_trap_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	else {
		make_intr_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}

/*
 * 외부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록한다.
 * 디버깅 목적으로 이름은 NAME으로 붙인다.
 * 이 핸들러는 인터럽트가 비활성화된 상태에서 실행된다.
 */
void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/*
 * 내부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록한다.
 * 디버깅 목적으로 이름은 NAME으로 붙인다.
 * 인터럽트 핸들러는 인터럽트 상태를 LEVEL로 설정한 채 호출된다.
 *
 * 이 핸들러는 descriptor privilege level DPL을 가지며,
 * 이는 프로세서가 DPL 또는 그보다 더 낮은 번호의 ring에 있을 때
 * 의도적으로 호출될 수 있음을 뜻한다. 실제로는 DPL==3이면 유저 모드가
 * 인터럽트를 호출할 수 있고, DPL==0이면 그런 호출을 막는다.
 * 유저 모드에서 발생한 fault와 exception은 여전히 DPL==0인 인터럽트를
 * 호출하게 된다. 자세한 내용은 [IA32-v3a]의 4.5절 "Privilege Levels"와
 * 4.8.1.1절 "Accessing Nonconforming Code Segments"를 참고하라.
 */
void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/*
 * 외부 인터럽트를 처리하는 동안에는 true를,
 * 그 외의 모든 때에는 false를 반환한다.
 */
bool
intr_context (void) {
	return in_external_intr;
}

/*
 * 외부 인터럽트를 처리하는 동안, 인터럽트 핸들러가 인터럽트에서 반환되기 직전에
 * 새 프로세스에 양보하도록 지시한다. 다른 시점에는 호출할 수 없다.
 */
void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}

/*
 * 8259A Programmable Interrupt Controller.
 */

/*
 * 모든 PC는 8259A Programmable Interrupt Controller(PIC) 칩 두 개를 가진다.
 * 하나는 0x20과 0x21 포트로 접근 가능한 "master"이고,
 * 다른 하나는 master의 IRQ 2 라인에 직렬 연결된 "slave"로
 * 0xa0과 0xa1 포트로 접근할 수 있다. 0x20 포트에 접근하면 A0 라인이 0이 되고,
 * 0x21 포트에 접근하면 A1 라인이 1이 된다. slave PIC도 상황은 비슷하다.
 *
 * 기본적으로 PIC가 전달하는 인터럽트 0...15는 인터럽트 벡터 0...15로 간다.
 * 불행히도 이 벡터들은 CPU trap과 exception에도 사용된다.
 * 따라서 우리는 PIC를 재프로그래밍해서 인터럽트 0...15가 대신
 * 인터럽트 벡터 32...47(0x20...0x2f)로 전달되도록 한다.
 */

/*
 * PIC들을 초기화한다. 자세한 내용은 [8259A]를 참고하라.
 */
static void
pic_init (void) {
	/*
	 * 두 PIC의 모든 인터럽트를 마스킹한다.
	 */
	outb (0x21, 0xff);
	outb (0xa1, 0xff);

	/*
	 * master를 초기화한다.
	 */
	outb (0x20, 0x11); /* ICW1: single mode, edge triggered, expect ICW4. */
	outb (0x21, 0x20); /* ICW2: line IR0...7 -> irq 0x20...0x27. */
	outb (0x21, 0x04); /* ICW3: slave PIC on line IR2. */
	outb (0x21, 0x01); /* ICW4: 8086 mode, normal EOI, non-buffered. */

	/*
	 * slave를 초기화한다.
	 */
	outb (0xa0, 0x11); /* ICW1: single mode, edge triggered, expect ICW4. */
	outb (0xa1, 0x28); /* ICW2: line IR0...7 -> irq 0x28...0x2f. */
	outb (0xa1, 0x02); /* ICW3: slave ID is 2. */
	outb (0xa1, 0x01); /* ICW4: 8086 mode, normal EOI, non-buffered. */

	/*
	 * 모든 인터럽트 마스크를 해제한다.
	 */
	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}

/*
 * 주어진 IRQ에 대해 PIC에 end-of-interrupt 신호를 보낸다.
 * IRQ를 acknowledge하지 않으면 다시는 우리에게 전달되지 않으므로 중요하다.
 */
static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);

	/*
	 * master PIC를 acknowledge한다.
	 */
	outb (0x20, 0x20);

	/*
	 * 이것이 slave 인터럽트라면 slave PIC도 acknowledge한다.
	 */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}
/*
 * 인터럽트 핸들러들.
 */

/*
 * 모든 인터럽트, fault, exception을 처리하는 핸들러.
 * 이 함수는 intr-stubs.S 안의 어셈블리 인터럽트 스텁들에 의해 호출된다.
 * FRAME은 인터럽트 정보와 인터럽트된 스레드의 레지스터들을 설명한다.
 */
void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;

	/*
	 * 외부 인터럽트는 특별하다.
	 * 한 번에 하나만 처리하며(따라서 인터럽트는 꺼져 있어야 한다),
	 * PIC에서 acknowledge도 해 주어야 한다(아래 참고).
	 * 외부 인터럽트 핸들러는 sleep할 수 없다.
	 */
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;
		yield_on_return = false;
	}

	/*
	 * 인터럽트 핸들러를 호출한다.
	 */
	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
		/*
		 * 핸들러는 없지만, 이 인터럽트는 하드웨어 fault나 하드웨어 race condition
		 * 때문에 가짜로 발생할 수 있다. 무시한다.
		 */
	} else {
		/*
		 * 핸들러도 없고 가짜 인터럽트도 아니다.
		 * 예상하지 못한 인터럽트 핸들링으로 처리한다.
		 */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	/*
	 * 외부 인터럽트 처리를 마무리한다.
	 */
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;
		pic_end_of_interrupt (frame->vec_no);

		if (yield_on_return)
			thread_yield ();
	}
}

/*
 * 디버깅을 위해 인터럽트 프레임 F를 콘솔에 출력한다.
 */
void
intr_dump_frame (const struct intr_frame *f) {
	/*
	 * CR2는 마지막 페이지 폴트의 선형 주소다.
	 * [IA32-v2a]의 "MOV--Move to/from Control Registers"와
	 * [IA32-v3a] 5.14절 "Interrupt 14--Page Fault Exception (#PF)"를 참고하라.
	 */
	uint64_t cr2 = rcr2();
	printf ("Interrupt %#04llx (%s) at rip=%llx\n",
			f->vec_no, intr_names[f->vec_no], f->rip);
	printf (" cr2=%016llx error=%16llx\n", cr2, f->error_code);
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}

/*
 * 인터럽트 VEC의 이름을 반환한다.
 */
const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
