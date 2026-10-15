# Pintos Project 1: Threads — 전체 개요

## 프로젝트 목표

Pintos의 최소 스레드 시스템을 확장하여 **동기화(synchronization)** 의 핵심 문제를 직접 체험하고 해결한다.
모든 작업은 `threads/` 디렉터리를 중심으로 진행하며, `devices/timer.c` 도 수정 대상이다.

구현해야 할 세 가지 과제는 다음과 같다.

| # | 과제 | 핵심 키워드 | 난이도 |
|---|------|-----------|--------|
| 1 | Alarm Clock | busy-wait 제거, sleep list | 하 |
| 2 | Priority Scheduling | 선점, priority donation, nested donation | 상 |
| 3 | Advanced Scheduler (MLFQS) | 4.4BSD, fixed-point, nice/recent_cpu/load_avg | 중 |

세 과제 모두 필수이며 옵션 사항은 없다.

---

## Pintos 아키텍처 개관

Pintos는 x86-64 아키텍처를 타겟으로 하는 교육용 OS 프레임워크이며, 실제 하드웨어 대신 **QEMU** 에뮬레이터 위에서 동작한다.

```
pintos/
├── threads/        ← Project 1 핵심 작업 디렉터리
│   ├── thread.c/h  ← struct thread, 스케줄러, thread_create 등
│   ├── synch.c/h   ← semaphore, lock, condition variable
│   ├── init.c      ← 커널 main(), 부팅 시퀀스
│   ├── interrupt.c ← 인터럽트 on/off, 핸들러 등록
│   ├── palloc.c    ← 4KB 페이지 할당기
│   └── malloc.c    ← 커널 malloc/free
├── devices/
│   ├── timer.c/h   ← 시스템 타이머 (100 ticks/sec), timer_sleep 수정 대상
│   └── ...         ← vga, serial, kbd 등
├── include/
│   ├── threads/    ← 헤더 파일
│   └── lib/kernel/ ← list.h (doubly-linked list) — 반드시 숙지
├── lib/            ← 표준 C 라이브러리 서브셋
├── tests/          ← 테스트 케이스
└── utils/          ← pintos 실행 스크립트
```

---

## struct thread 구조 (현재 상태)

```c
struct thread {
    tid_t tid;                          // 스레드 ID
    enum thread_status status;          // RUNNING, READY, BLOCKED, DYING
    char name[16];                      // 디버깅용 이름
    int priority;                       // 우선순위 (0~63)
    struct list_elem elem;              // ready_list 또는 semaphore waiters 용
    struct intr_frame tf;               // 컨텍스트 스위칭 정보
    unsigned magic;                     // 스택 오버플로 감지
};
```

각 스레드는 **4KB 페이지** 하나를 차지한다. 구조체는 페이지 하단(offset 0)에, 커널 스택은 페이지 상단(4KB)에서 아래로 자란다. 따라서 큰 지역 변수를 쓰면 스택 오버플로가 발생하여 `magic` 값이 훼손되고 assertion failure가 터진다.

---

## 스레드 생명주기

```
                    thread_create()
                         │
                         ▼
              ┌──── BLOCKED ────┐
              │   (초기 상태)    │
              │                 │
    thread_unblock()         thread_block()
              │                 │
              ▼                 │
           READY ◄──────────────┘
              │
    schedule() — next_thread_to_run()
              │
              ▼
          RUNNING
              │
    thread_exit()
              │
              ▼
           DYING → 메모리 해제
```

---

## 스케줄러 동작 원리

### schedule() 함수 흐름

```c
static void schedule(void) {
    struct thread *curr = running_thread();
    struct thread *next = next_thread_to_run();  // ready_list에서 꺼냄

    ASSERT(intr_get_level() == INTR_OFF);        // 인터럽트 반드시 OFF
    ASSERT(curr->status != THREAD_RUNNING);      // 이미 상태 변경된 상태

    next->status = THREAD_RUNNING;
    thread_ticks = 0;                            // 새 타임 슬라이스 시작

    if (curr != next)
        thread_launch(next);                     // 컨텍스트 스위칭
}
```

현재 `next_thread_to_run()`은 `list_pop_front(&ready_list)`로 **FIFO** 방식이다. 이것을 우선순위 기반으로 바꿔야 한다.

### 스케줄러 호출 시점

1. **thread_yield()** — 현재 스레드가 자발적으로 양보
2. **thread_block()** — 세마포어/락 대기 등으로 블록
3. **thread_exit()** — 스레드 종료
4. **thread_tick()** — 타이머 인터럽트에서 `TIME_SLICE`(4틱) 초과 시 `intr_yield_on_return()`

---

## 타이머 시스템

- `TIMER_FREQ` = 100 (초당 100틱, 1틱 = 10ms)
- `timer_interrupt()` → 매 틱마다 `ticks++` 및 `thread_tick()` 호출
- `timer_sleep(ticks)` — 현재는 busy-wait 방식 (수정 대상)

---

## 동기화 프리미티브 (현재 제공)

| 프리미티브 | 구조 | 핵심 동작 |
|-----------|------|----------|
| **Semaphore** | `{value, waiters}` | `sema_down`: value > 0 될 때까지 블록 후 감소, `sema_up`: 값 증가 + 대기 스레드 깨움 |
| **Lock** | `{holder, semaphore}` | value=1인 세마포어 + 소유자 추적. 같은 스레드가 acquire/release |
| **Condition Variable** | `{waiters}` | Mesa-style 모니터. `cond_wait`: 락 해제 후 대기, `cond_signal`: 하나 깨움 |
| **Optimization Barrier** | `barrier()` | 컴파일러 리오더링 방지 |

---

## 개발 환경

- **OS**: Ubuntu 22.04 LTS (x86_64) 필수
- **에뮬레이터**: QEMU
- **Docker**: `krafton-jungle/pintos_22.04_lab_docker` 사용 가능
- **빌드**: `threads/` 디렉터리에서 `make` → `build/` 디렉터리 생성
- **테스트**: `make check` 또는 `pintos -- run <test-name>`

---

## 핵심 유의사항

1. **인터럽트 비활성화는 최소화**: 타이머 틱이나 입력 이벤트를 놓칠 수 있다. 동기화는 세마포어/락/CV를 우선 사용한다.
2. **busy-wait 금지**: `thread_yield()`를 루프에서 호출하는 패턴은 CPU 낭비.
3. **스택 크기 제한**: 4KB 내에서 동작해야 함. 큰 배열은 `malloc()`이나 `palloc_get_page()` 사용.
4. **list_entry 매크로 이해 필수**: `offsetof`로 `list_elem` 포인터에서 부모 구조체 포인터를 역산한다.

---

## 문서 구성

| 파일 | 내용 |
|------|------|
| `01-big-picture.md` | 전체 흐름 Big Picture |
| `02-overview.md` | 이 문서 — 전체 개요 |
| `03-data-structures.md` | Pintos 자료구조 (list, list_entry, offsetof) |
| `04-synchronization.md` | 동기화 프리미티브 상세 분석 |
| `05-alarm-clock.md` | Alarm Clock 구현 가이드 |
| `06-priority-scheduling.md` | Priority Scheduling + Priority Donation |
| `07-advanced-scheduler.md` | MLFQS (4.4BSD 스케줄러) |
| `08-implementation-guide.md` | 구현 순서 및 전략 가이드 |
| `09-team-strategy.md` | 팀 협업 전략 |
