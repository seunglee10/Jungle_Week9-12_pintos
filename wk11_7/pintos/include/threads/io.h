/*
 * 이 파일은 MIT의 6.828 강의에서 사용된 소스 코드에서 파생되었다.
 * 원래의 저작권 고지는 아래에 전문이 재수록되어 있다.
 */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology
 *
 * 이 소프트웨어는 저작권 보유자가 다음 라이선스에 따라 제공한다.
 * 이 소프트웨어를 취득, 사용 또는 복사함으로써, 사용자는 아래의 조건을
 * 읽고 이해했으며 이를 준수하는 데 동의한 것으로 간주된다.
 *
 * 본 고지의 전문이 사용자가 만드는 소프트웨어와 문서의 모든 사본 또는 그 일부,
 * 그리고 수정본을 포함해 모두에 표시되는 한, 어떤 목적이든 수수료나 로열티 없이
 * 이 소프트웨어와 그 문서를 사용, 복사, 수정, 배포, 판매할 수 있는 권한을
 * 부여한다.
 *
 * 이 소프트웨어는 "있는 그대로" 제공되며, 저작권 보유자는 명시적이든
 * 묵시적이든 어떠한 진술이나 보증도 하지 않는다. 예시로서만 들자면,
 * 이에 국한되지 않으며, 저작권 보유자는 상품성이나 특정 목적 적합성에 대한
 * 어떠한 보증도 하지 않고, 이 소프트웨어나 문서의 사용이 제3자의 특허권,
 * 저작권, 상표권 또는 기타 권리를 침해하지 않는다는 보증도 하지 않는다.
 * 저작권 보유자는 이 소프트웨어나 문서의 사용에 대해 어떠한 책임도 지지 않는다.
 *
 * 저작권 보유자의 이름과 상표는 사전에 구체적인 서면 허가 없이
 * 이 소프트웨어와 관련된 광고나 홍보에 사용되어서는 안 된다.
 * 이 소프트웨어와 관련 문서의 저작권은 항상 저작권 보유자에게 남는다.
 * 모든 저작권 보유자 목록은 이 소프트웨어와 함께 제공되었어야 하는 AUTHORS
 * 파일을 참고하라.
 *
 * 이 파일은 기존에 저작권이 있는 소프트웨어에서 파생되었을 수 있다.
 * 이 저작권은 AUTHORS 파일에 나열된 저작권 보유자가 변경한 부분에만 적용된다.
 * 나머지 부분은 아래에 있을 수 있는 저작권 고지의 적용을 받는다.
 */

#ifndef THREADS_IO_H
#define THREADS_IO_H

#include <stddef.h>
#include <stdint.h>

/*
 * PORT에서 1바이트를 읽어 반환한다.
 */
static inline uint8_t
inb (uint16_t port) {
	/*
	 * [IA32-v2a]의 "IN"을 참고하라.
	 */
	uint8_t data;
	asm volatile ("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/*
 * PORT에서 CNT 바이트를 차례대로 읽어
 * ADDR에서 시작하는 버퍼에 저장한다.
 */
static inline void
insb (uint16_t port, void *addr, size_t cnt) {
	/*
	 * [IA32-v2a]의 "INS"를 참고하라.
	 */
	asm volatile ("cld; repne; insb"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/*
 * PORT에서 16비트를 읽어 반환한다.
 */
static inline uint16_t
inw (uint16_t port) {
	uint16_t data;
	/*
	 * [IA32-v2a]의 "IN"을 참고하라.
	 */
	asm volatile ("inw %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/*
 * PORT에서 CNT개의 16비트(halfword) 단위를 차례대로 읽어
 * ADDR에서 시작하는 버퍼에 저장한다.
 */
static inline void
insw (uint16_t port, void *addr, size_t cnt) {
	/*
	 * [IA32-v2a]의 "INS"를 참고하라.
	 */
	asm volatile ("cld; repne; insw"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/*
 * PORT에서 32비트를 읽어 반환한다.
 */
static inline uint32_t
inl (uint16_t port) {
	/*
	 * [IA32-v2a]의 "IN"을 참고하라.
	 */
	uint32_t data;
	asm volatile ("inl %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/*
 * PORT에서 CNT개의 32비트(word) 단위를 차례대로 읽어
 * ADDR에서 시작하는 버퍼에 저장한다.
 */
static inline void
insl (uint16_t port, void *addr, size_t cnt) {
	/*
	 * [IA32-v2a]의 "INS"를 참고하라.
	 */
	asm volatile ("cld; repne; insl"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/*
 * DATA 1바이트를 PORT에 기록한다.
 */
static inline void
outb (uint16_t port, uint8_t data) {
	/*
	 * [IA32-v2b]의 "OUT"을 참고하라.
	 */
	asm volatile ("outb %0,%w1" : : "a" (data), "d" (port));
}

/*
 * ADDR에서 시작하는 CNT 바이트 버퍼의 각 바이트를 PORT에 기록한다.
 */
static inline void
outsb (uint16_t port, const void *addr, size_t cnt) {
	/*
	 * [IA32-v2b]의 "OUTS"를 참고하라.
	 */
	asm volatile ("cld; repne; outsb"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

/*
 * 16비트 DATA를 PORT에 기록한다.
 */
static inline void
outw (uint16_t port, uint16_t data) {
	/*
	 * [IA32-v2b]의 "OUT"을 참고하라.
	 */
	asm volatile ("outw %0,%w1" : : "a" (data), "d" (port));
}

/*
 * ADDR에서 시작하는 CNT halfword 버퍼의 각 16비트 단위를 PORT에 기록한다.
 */
static inline void
outsw (uint16_t port, const void *addr, size_t cnt) {
	/*
	 * [IA32-v2b]의 "OUTS"를 참고하라.
	 */
	asm volatile ("cld; repne; outsw"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

/*
 * 32비트 DATA를 PORT에 기록한다.
 */
static inline void
outl (uint16_t port, uint32_t data) {
	/*
	 * [IA32-v2b]의 "OUT"을 참고하라.
	 */
	asm volatile ("outl %0,%w1" : : "a" (data), "d" (port));
}

/*
 * ADDR에서 시작하는 CNT word 버퍼의 각 32비트 단위를 PORT에 기록한다.
 */
static inline void
outsl (uint16_t port, const void *addr, size_t cnt) {
	/*
	 * [IA32-v2b]의 "OUTS"를 참고하라.
	 */
	asm volatile ("cld; repne; outsl"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

#endif /* threads/io.h */
