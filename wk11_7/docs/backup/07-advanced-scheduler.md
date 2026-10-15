# Advanced Scheduler (MLFQS) 구현 가이드

## 개요

4.4BSD 스케줄러를 기반으로 한 **다단계 피드백 큐 스케줄러(MLFQS)** 를 구현한다. 커널 부팅 시 `-mlfqs` 옵션으로 활성화되며, `thread_mlfqs` 전역 변수가 `true`로 설정된다.

MLFQS 모드에서는 **우선순위를 스케줄러가 동적으로 결정**하므로 `thread_set_priority()`와 priority donation은 무시된다.

---

## 핵심 구성 요소

| 요소 | 범위 | 설명 |
|------|------|------|
| **priority** | 0~63 | 스케줄러가 계산. 64개 큐에 대응 |
| **nice** | -20~+20 | 스레드별 "양보" 수준. 양수면 양보, 음수면 공격적 |
| **recent_cpu** | 실수(고정소수점) | 최근 CPU 사용량. 매 틱 증가 |
| **load_avg** | 실수(고정소수점) | 시스템 전체 부하. 1분간 이동평균 |

---

## 공식 정리

### 1. Priority 재계산 (매 4번째 틱)

```
priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
```

- 결과는 정수로 **내림(truncate)** 처리
- 범위를 [PRI_MIN, PRI_MAX] 로 **클램핑**
- **모든 스레드**에 대해 매 4틱마다 재계산

### 2. recent_cpu 업데이트

**매 틱 (실행 중인 스레드만):**
```
recent_cpu = recent_cpu + 1
```
(idle 스레드 제외)

**매 초 (timer_ticks() % TIMER_FREQ == 0, 모든 스레드):**
```
coefficient = (2 * load_avg) / (2 * load_avg + 1)
recent_cpu = coefficient * recent_cpu + nice
```

- 음수값 허용 (클램핑하지 않음)
- **coefficient를 먼저 계산**하여 오버플로 방지

### 3. load_avg 업데이트 (매 초, 시스템 전체)

```
load_avg = (59/60) * load_avg + (1/60) * ready_threads
```

- `ready_threads` = 실행 중 + ready 상태 스레드 수 (idle 스레드 제외)
- 초기값: 0
- `timer_ticks() % TIMER_FREQ == 0` 시점에 업데이트

---

## Fixed-Point 산술

커널에서는 부동소수점을 사용할 수 없으므로 **17.14 고정소수점** 형식을 사용한다.

### 형식

```
부호(1비트) | 정수부(17비트) | 소수부(14비트)
   31      |  30 ~ 14       |  13 ~ 0
```

```c
#define F (1 << 14)  // 2^14 = 16384
```

### 연산 테이블

| 연산 | C 구현 |
|------|--------|
| 정수 n → 고정소수점 | `n * F` |
| 고정소수점 x → 정수 (0 방향 절삭) | `x / F` |
| 고정소수점 x → 정수 (반올림) | `(x >= 0) ? (x + F/2) / F : (x - F/2) / F` |
| 고정소수점 x + y | `x + y` |
| 고정소수점 x - y | `x - y` |
| 고정소수점 x + 정수 n | `x + n * F` |
| 고정소수점 x - 정수 n | `x - n * F` |
| 고정소수점 x * y | `((int64_t) x) * y / F` |
| 고정소수점 x / y | `((int64_t) x) * F / y` |
| 고정소수점 x * 정수 n | `x * n` |
| 고정소수점 x / 정수 n | `x / n` |

> **오버플로 주의**: `x * y` 연산에서 중간값이 `int32_t` 범위를 넘을 수 있으므로 반드시 `int64_t`로 캐스팅 후 곱한다.

### 구현 방식

헤더 파일로 매크로 또는 인라인 함수를 정의하는 것이 깔끔하다.

```c
/* threads/fixed_point.h (새 파일) */
#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define FP_F (1 << 14)

#define FP_INT_TO_FP(n)      ((n) * FP_F)
#define FP_FP_TO_INT(x)      ((x) / FP_F)
#define FP_FP_TO_INT_ROUND(x) \
    ((x) >= 0 ? ((x) + FP_F / 2) / FP_F : ((x) - FP_F / 2) / FP_F)

#define FP_ADD(x, y)         ((x) + (y))
#define FP_SUB(x, y)         ((x) - (y))
#define FP_ADD_INT(x, n)     ((x) + (n) * FP_F)
#define FP_SUB_INT(x, n)     ((x) - (n) * FP_F)

#define FP_MUL(x, y)         ((int)(((int64_t)(x)) * (y) / FP_F))
#define FP_DIV(x, y)         ((int)(((int64_t)(x)) * FP_F / (y)))
#define FP_MUL_INT(x, n)     ((x) * (n))
#define FP_DIV_INT(x, n)     ((x) / (n))

#endif
```

---

## struct thread 확장 (MLFQS용)

```c
struct thread {
    // ... 기존 필드 ...
    int nice;          // nice 값 (-20 ~ +20)
    int recent_cpu;    // 최근 CPU 사용량 (fixed-point)
    // ...
};
```

### 전역 변수

```c
int load_avg;  // 시스템 전체 부하 평균 (fixed-point), 초기값 0
```

---

## 구현할 함수

### thread_set_nice(int nice)

```c
void thread_set_nice(int nice) {
    enum intr_level old_level = intr_disable();
    thread_current()->nice = nice;
    mlfqs_recalc_priority(thread_current());
    test_max_priority();  // 선점 체크
    intr_set_level(old_level);
}
```

### thread_get_nice()

```c
int thread_get_nice(void) {
    return thread_current()->nice;
}
```

### thread_get_load_avg()

```c
int thread_get_load_avg(void) {
    return FP_FP_TO_INT_ROUND(FP_MUL_INT(load_avg, 100));
}
```

### thread_get_recent_cpu()

```c
int thread_get_recent_cpu(void) {
    return FP_FP_TO_INT_ROUND(
        FP_MUL_INT(thread_current()->recent_cpu, 100));
}
```

---

## timer_interrupt() 내 스케줄러 업데이트

```c
static void timer_interrupt(struct intr_frame *args UNUSED) {
    ticks++;
    thread_tick();

    // Alarm Clock 처리
    thread_awake(ticks);

    if (thread_mlfqs) {
        // 매 틱: 실행 중 스레드의 recent_cpu 증가
        mlfqs_increment_recent_cpu();

        // 매 초 (TIMER_FREQ 틱마다): load_avg, 모든 스레드 recent_cpu 재계산
        if (ticks % TIMER_FREQ == 0) {
            mlfqs_recalc_load_avg();
            mlfqs_recalc_recent_cpu_all();
        }

        // 매 4틱: 모든 스레드 priority 재계산
        if (ticks % 4 == 0) {
            mlfqs_recalc_priority_all();
        }
    }
}
```

### 각 함수 구현

```c
void mlfqs_increment_recent_cpu(void) {
    struct thread *curr = thread_current();
    if (curr != idle_thread)
        curr->recent_cpu = FP_ADD_INT(curr->recent_cpu, 1);
}

void mlfqs_recalc_load_avg(void) {
    int ready_count = list_size(&ready_list);
    if (thread_current() != idle_thread)
        ready_count++;

    // load_avg = (59/60) * load_avg + (1/60) * ready_count
    load_avg = FP_ADD(
        FP_MUL(FP_DIV(FP_INT_TO_FP(59), 60), load_avg),
        FP_MUL_INT(FP_DIV(FP_INT_TO_FP(1), 60), ready_count)
    );
}

void mlfqs_recalc_recent_cpu(struct thread *t) {
    // coefficient = (2 * load_avg) / (2 * load_avg + 1)
    int twice_load = FP_MUL_INT(load_avg, 2);
    int coefficient = FP_DIV(twice_load, FP_ADD_INT(twice_load, 1));
    t->recent_cpu = FP_ADD_INT(FP_MUL(coefficient, t->recent_cpu), t->nice);
}

void mlfqs_recalc_priority(struct thread *t) {
    // priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
    int pri = PRI_MAX
              - FP_FP_TO_INT(FP_DIV_INT(t->recent_cpu, 4))
              - (t->nice * 2);
    if (pri > PRI_MAX) pri = PRI_MAX;
    if (pri < PRI_MIN) pri = PRI_MIN;
    t->priority = pri;
}
```

---

## MLFQS 모드에서 비활성화할 것

```c
void thread_set_priority(int new_priority) {
    if (thread_mlfqs) return;  // MLFQS에서는 무시
    // ... 기존 로직 ...
}
```

- `thread_create()`에서 전달받은 priority 파라미터도 MLFQS 모드에서는 무시 (스케줄러가 계산)
- Priority donation 로직도 MLFQS 모드에서는 동작하지 않아야 함

---

## 업데이트 타이밍 정리

| 주기 | 대상 | 연산 |
|------|------|------|
| **매 틱** | 현재 실행 스레드 | `recent_cpu += 1` |
| **매 4틱** | 모든 스레드 | `priority` 재계산 |
| **매 초** (TIMER_FREQ) | 시스템 전체 | `load_avg` 재계산 |
| **매 초** (TIMER_FREQ) | 모든 스레드 | `recent_cpu` 재계산 |

> 매 초 업데이트 시: load_avg → recent_cpu → priority 순서로 계산해야 한다.

---

## 초기화

```c
void init_thread(struct thread *t, const char *name, int priority) {
    // ... 기존 코드 ...
    t->nice = 0;
    t->recent_cpu = FP_INT_TO_FP(0);
}
```

- 초기 스레드(main)의 `nice = 0`, `recent_cpu = 0`
- 새 스레드는 부모의 `nice`와 `recent_cpu`를 상속
- `load_avg` 초기값 = 0

---

## 관련 테스트

```
mlfqs-load-1          — 단일 스레드 load_avg
mlfqs-load-60         — 60개 스레드 load_avg
mlfqs-load-avg        — load_avg 정확도
mlfqs-recent-1        — recent_cpu 단일
mlfqs-fair-*          — nice 값에 따른 공정성
mlfqs-nice-2          — nice 값 변경
mlfqs-nice-10         — 10개 스레드 nice
mlfqs-block           — 블록된 스레드의 recent_cpu
```

---

## 수정 대상 파일 요약

| 파일 | 수정 내용 |
|------|----------|
| `include/threads/thread.h` | `nice`, `recent_cpu` 필드 추가 |
| `threads/thread.c` | MLFQS 관련 함수들, `thread_tick()`, `init_thread()` |
| `threads/fixed_point.h` | **새 파일** — 고정소수점 매크로 |
| `devices/timer.c` | `timer_interrupt()`에 MLFQS 업데이트 호출 추가 |
