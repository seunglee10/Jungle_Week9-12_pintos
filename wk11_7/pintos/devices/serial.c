#include "devices/serial.h"
#include <debug.h>
#include "devices/input.h"
#include "devices/intq.h"
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* PC에서 사용하는 16550A UART의 레지스터 정의.
   16550A는 여기 나온 것보다 훨씬 많은 기능이 있지만,
   지금 구현에는 이것만 있으면 충분하다.

   하드웨어 정보는 [PC16650D]를 참고한다. */

/* 첫 번째 직렬 포트의 I/O 포트 기준 주소. */
#define IO_BASE 0x3f8

/* DLAB=0일 때의 레지스터. */
#define RBR_REG (IO_BASE + 0)   /* 수신 버퍼 레지스터(읽기 전용). */
#define THR_REG (IO_BASE + 0)   /* 송신 보관 레지스터(쓰기 전용). */
#define IER_REG (IO_BASE + 1)   /* 인터럽트 활성화 레지스터. */

/* DLAB=1일 때의 레지스터. */
#define LS_REG (IO_BASE + 0)    /* 분주 래치(하위 바이트). */
#define MS_REG (IO_BASE + 1)    /* 분주 래치(상위 바이트). */

/* DLAB 값과 무관한 레지스터. */
#define IIR_REG (IO_BASE + 2)   /* 인터럽트 식별 레지스터(읽기 전용). */
#define FCR_REG (IO_BASE + 2)   /* FIFO 제어 레지스터(쓰기 전용). */
#define LCR_REG (IO_BASE + 3)   /* 라인 제어 레지스터. */
#define MCR_REG (IO_BASE + 4)   /* MODEM 제어 레지스터. */
#define LSR_REG (IO_BASE + 5)   /* 라인 상태 레지스터(읽기 전용). */

/* 인터럽트 활성화 레지스터 비트. */
#define IER_RECV 0x01           /* 데이터 수신 시 인터럽트. */
#define IER_XMIT 0x02           /* 송신 완료 시 인터럽트. */

/* 라인 제어 레지스터 비트. */
#define LCR_N81 0x03            /* 패리티 없음, 데이터 8비트, 정지 비트 1개. */
#define LCR_DLAB 0x80           /* 분주 래치 접근 비트(DLAB). */

/* MODEM 제어 레지스터. */
#define MCR_OUT2 0x08           /* 출력 라인 2. */

/* 라인 상태 레지스터. */
#define LSR_DR 0x01             /* 데이터 준비 완료: 수신 바이트가 RBR에 있음. */
#define LSR_THRE 0x20           /* THR 비어 있음. */

/* 전송 모드. */
static enum { UNINIT, POLL, QUEUE } mode;

/* 전송할 데이터. */
static struct intq txq;

static void set_serial (int bps);
static void putc_poll (uint8_t);
static void write_ier (void);
static intr_handler_func serial_interrupt;

/* 직렬 포트를 폴링 모드로 초기화한다.
   폴링 모드에서는 직렬 포트가 비워질 때까지 바쁘게 기다렸다가 쓴다.
   느리지만 인터럽트가 초기화되기 전에는 이 방법밖에 없다. */
static void
init_poll (void) {
	ASSERT (mode == UNINIT);
	outb (IER_REG, 0);                    /* 모든 인터럽트 끄기. */
	outb (FCR_REG, 0);                    /* FIFO 비활성화. */
	set_serial (115200);                  /* 115.2 kbps, N-8-1. */
	outb (MCR_REG, MCR_OUT2);             /* 인터럽트 활성화에 필요. */
	intq_init (&txq);
	mode = POLL;
}

/* 직렬 포트를 큐 기반 인터럽트 구동 I/O 모드로 초기화한다.
   인터럽트 기반 I/O에서는 직렬 장치가 준비될 때까지 CPU를 낭비하며
   기다릴 필요가 없다. */
void
serial_init_queue (void) {
	enum intr_level old_level;

	if (mode == UNINIT)
		init_poll ();
	ASSERT (mode == POLL);

	intr_register_ext (0x20 + 4, serial_interrupt, "serial");
	mode = QUEUE;
	old_level = intr_disable ();
	write_ier ();
	intr_set_level (old_level);
}

/* BYTE를 직렬 포트로 보낸다. */
void
serial_putc (uint8_t byte) {
	enum intr_level old_level = intr_disable ();

	if (mode != QUEUE) {
		/* 아직 인터럽트 기반 I/O를 쓸 수 없으면
		   단순 폴링으로 바이트를 전송한다. */
		if (mode == UNINIT)
			init_poll ();
		putc_poll (byte);
	} else {
		/* 그 외에는 바이트를 큐에 넣고 인터럽트 활성화
		   레지스터를 갱신한다. */
		if (old_level == INTR_OFF && intq_full (&txq)) {
			/* 인터럽트가 꺼져 있고 전송 큐도 가득 찼다.
			   큐가 빌 때까지 기다리려면 인터럽트를 다시 켜야 하는데,
			   그건 바람직하지 않으므로 대신 폴링으로 한 글자를 보낸다. */
			putc_poll (intq_getc (&txq));
		}

		intq_putc (&txq, byte);
		write_ier ();
	}

	intr_set_level (old_level);
}

/* 직렬 버퍼에 남은 내용을 폴링 방식으로 포트 밖으로 모두 내보낸다. */
void
serial_flush (void) {
	enum intr_level old_level = intr_disable ();
	while (!intq_empty (&txq))
		putc_poll (intq_getc (&txq));
	intr_set_level (old_level);
}

/* 입력 버퍼의 여유 상태가 바뀌었을 수 있다.
   수신 인터럽트를 막아야 하는지 다시 판단한다.
   입력 버퍼 루틴이 문자를 넣거나 뺄 때 호출된다. */
void
serial_notify (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	if (mode == QUEUE)
		write_ier ();
}

/* 직렬 포트를 초당 BPS 비트 전송 속도로 설정한다. */
static void
set_serial (int bps) {
	int base_rate = 1843200 / 16;         /* 16550A의 기본 주파수(Hz). */
	uint16_t divisor = base_rate / bps;   /* 클록 분주 값. */

	ASSERT (bps >= 300 && bps <= 115200);

	/* DLAB 활성화. */
	outb (LCR_REG, LCR_N81 | LCR_DLAB);

	/* 전송 속도 설정. */
	outb (LS_REG, divisor & 0xff);
	outb (MS_REG, divisor >> 8);

	/* DLAB 해제. */
	outb (LCR_REG, LCR_N81);
}

/* 인터럽트 활성화 레지스터를 갱신한다. */
static void
write_ier (void) {
	uint8_t ier = 0;

	ASSERT (intr_get_level () == INTR_OFF);

	/* 전송할 문자가 하나라도 있으면 송신 인터럽트를 켠다. */
	if (!intq_empty (&txq))
		ier |= IER_XMIT;

	/* 수신 문자를 저장할 공간이 있으면 수신 인터럽트를 켠다. */
	if (!input_full ())
		ier |= IER_RECV;

	outb (IER_REG, ier);
}

/* 직렬 포트가 준비될 때까지 폴링한 뒤 BYTE를 전송한다. */
static void
putc_poll (uint8_t byte) {
	ASSERT (intr_get_level () == INTR_OFF);

	while ((inb (LSR_REG) & LSR_THRE) == 0)
		continue;
	outb (THR_REG, byte);
}

/* 직렬 포트 인터럽트 핸들러. */
static void
serial_interrupt (struct intr_frame *f UNUSED) {
	/* UART의 인터럽트 상태를 확인한다.
	   이 작업이 없으면 QEMU에서 가끔 인터럽트를 놓칠 수 있다. */
	inb (IIR_REG);

	/* 바이트를 저장할 공간이 있고 하드웨어에도 수신할 바이트가 있는 동안
	   계속 받아들인다. */
	while (!input_full () && (inb (LSR_REG) & LSR_DR) != 0)
		input_putc (inb (RBR_REG));

	/* 전송할 바이트가 있고 하드웨어도 바이트를 받을 준비가 된 동안
	   계속 전송한다. */
	while (!intq_empty (&txq) && (inb (LSR_REG) & LSR_THRE) != 0)
		outb (THR_REG, intq_getc (&txq));

	/* 큐 상태에 맞춰 인터럽트 활성화 레지스터를 갱신한다. */
	write_ier ();
}
