# Project 1 전체 그림: 무엇을 만드는가

## 한 줄 요약

타이머로 스레드를 재우고, 우선순위로 스레드를 고르고, 락으로 우선순위를 넘기고, 수식으로 우선순위를 자동 계산하는 스케줄러를 만든다.

---

## 전체 구조도

```mermaid
graph TD
    subgraph Kernel["Pintos Kernel"]
        subgraph Timer["timer.c"]
            TI["timer_interrupt<br/>(매 틱 호출)"]
            TS["timer_sleep<br/>(스레드 재우기)"]
            T1["<b>Phase 1</b><br/>sleep_list 관리<br/>깨우기 로직"]
            T4["<b>Phase 4</b><br/>MLFQS 업데이트 호출"]
        end

        subgraph Thread["thread.c"]
            TT["thread_tick<br/>(선점 판단)"]
            TC["thread_create"]
            TY["thread_yield"]
            TB["thread_block"]
            TU["thread_unblock"]
            Th2["<b>Phase 2</b><br/>우선순위 정렬<br/>선점 체크"]
            Th4["<b>Phase 4</b><br/>MLFQS 계산 함수"]
        end

        subgraph Synch["synch.c"]
            SE["semaphore"]
            LK["lock"]
            CV["condition var"]
            S2["<b>Phase 2</b><br/>우선순위 순 대기<br/>우선순위 순 깨움"]
            S3["<b>Phase 3</b><br/>priority donate<br/>donate 회수"]
        end
    end

    TI -->|"thread_tick()"| TT

    Thread --> Synch

```

---

## Phase별 의존 관계

```mermaid
graph TD
    P1["<b>Phase 1: Alarm Clock</b><br/>timer_sleep이 스레드를 BLOCKED로 만드는<br/>구조를 잡는다. 이 구조 위에서 나머지가 동작한다."]
    P2["<b>Phase 2: Priority Scheduling</b><br/>ready_list를 우선순위 순으로 정렬한다.<br/>semaphore/cond의 waiters도 우선순위 순으로 바꾼다.<br/>이것이 없으면 Phase 3의 기부가 의미 없다."]
    P3["<b>Phase 3: Priority Donation</b><br/>lock에서 우선순위 역전 문제를 해결한다.<br/>Phase 2의 정렬 구조 위에서 동작한다."]
    P4["<b>Phase 4: MLFQS</b><br/>Phase 2-3의 수동 우선순위 대신 자동 계산으로 교체한다.<br/>Phase 1의 timer_interrupt 구조를 그대로 활용한다."]
    DONE["완성"]

    P1 --> P2 --> P3 --> P4 --> DONE

```

각 Phase는 이전 Phase의 코드 위에 쌓인다.
Phase 1을 잘못 짜면 Phase 2-4 전부 흔들린다.

---

## 스레드 상태 전이와 각 Phase의 개입 지점

```mermaid
stateDiagram-v2
    [*] --> READY : thread_create()
    BLOCKED --> READY : thread_unblock()
    READY --> RUNNING : schedule()
    RUNNING --> READY : thread_yield()
    RUNNING --> BLOCKED : thread_block()<br/>sema_down()
    RUNNING --> DYING : thread_exit()

    note right of BLOCKED
        Phase 1 개입 지점
        - timer_sleep() -> thread_block()
        - timer_interrupt() -> thread_unblock()

        Phase 3 개입 지점
        - lock_acquire() 시 보유자에게 우선순위 기부
        - lock_release() 시 기부 회수 및 우선순위 복원
    end note

    note right of READY
        Phase 2 개입 지점
        - thread_unblock() 시 ready_list에 우선순위 순 삽입
        - schedule() 시 가장 높은 우선순위 선택
        - sema_down() 시 waiters에 우선순위 순 삽입
        - sema_up() 시 가장 높은 우선순위 깨움
    end note

    note right of RUNNING
        Phase 4 개입 지점
        - timer_interrupt() 매 틱: recent_cpu 증가
        - timer_interrupt() 매 4틱: priority 재계산
        - timer_interrupt() 매 초: load_avg, recent_cpu 재계산
    end note
```

---

## timer_interrupt: 모든 것의 시작점

하드웨어 타이머가 초당 100번 인터럽트를 발생시킨다.
이 하나의 함수가 Phase 1과 Phase 4의 진입점이다.

```mermaid
graph TD
    TI["timer_interrupt()<br/>(매 틱, 10ms마다 호출)"]
    TICK["ticks++"]
    TT["thread_tick()"]
    STAT["통계 업데이트<br/>(idle/kernel/user ticks)"]
    PREEMPT["thread_ticks >= TIME_SLICE(4)<br/>이면 선점 요청"]
    AWAKE["<b>Phase 1</b><br/>thread_awake(ticks)"]
    SLEEP["sleep_list 순회"]
    WAKE["wake_tick <= ticks 인<br/>스레드를 thread_unblock()"]
    MLFQS["<b>Phase 4</b><br/>MLFQS 업데이트<br/>(thread_mlfqs == true 일 때만)"]
    EVERY_TICK["매 틱: 현재 스레드<br/>recent_cpu += 1"]
    EVERY_SEC["ticks % TIMER_FREQ == 0 (매 초):<br/>load_avg 재계산<br/>모든 스레드 recent_cpu 재계산"]
    EVERY_4["ticks % 4 == 0 (매 4틱):<br/>모든 스레드 priority 재계산"]

    TI --> TICK --> TT
    TT --> STAT
    TT --> PREEMPT
    TI --> AWAKE
    AWAKE --> SLEEP --> WAKE
    TI --> MLFQS
    MLFQS --> EVERY_TICK
    MLFQS --> EVERY_SEC
    MLFQS --> EVERY_4

```

---

## schedule: 다음 스레드를 고르는 핵심

```
schedule()이 호출되는 경로 4가지:

1. thread_yield()    -> do_schedule(THREAD_READY)    -> schedule()
2. thread_block()    ->                                 schedule()
3. thread_exit()     -> do_schedule(THREAD_DYING)    -> schedule()
4. thread_tick()     -> intr_yield_on_return()        -> thread_yield() -> ...
```

```mermaid
graph TD
    START["schedule() 내부"]
    CURR["curr = running_thread()"]
    NEXT["next = next_thread_to_run()"]
    ORIG["원래: list_pop_front - ready_list<br/>FIFO, 먼저 들어온 스레드가 먼저 실행"]
    PH2["<b>Phase 2 이후</b>: ready_list가 이미<br/>우선순위 순으로 정렬되어 있으므로<br/>pop_front만 해도 최고 우선순위가 나온다"]
    SET["next->status = THREAD_RUNNING<br/>thread_ticks = 0"]
    SWITCH["if curr != next<br/>thread_launch(next)<br/>-- 컨텍스트 스위칭"]

    START --> CURR --> NEXT
    NEXT --> ORIG
    NEXT --> PH2
    ORIG --> SET
    PH2 --> SET
    SET --> SWITCH

```

---

## lock과 priority donation: Phase 3의 핵심 흐름

```mermaid
sequenceDiagram
    participant L as L (pri=1)
    participant LockA as Lock A
    participant H as H (pri=63)

    Note over L,LockA: L이 Lock A 획득<br/>holder = L

    H->>LockA: lock_acquire() 요청
    Note over H,L: L에게 63 기부<br/>L: pri 1 -> 63<br/>H: BLOCKED

    Note over L: L이 Lock A 보유 중 실행<br/>(M(32)보다 높은 63이므로 선점 안 당함)

    L->>LockA: lock_release()
    Note over L,H: H의 기부 제거<br/>L: pri 63 -> 1<br/>H: UNBLOCKED<br/>H가 Lock A 획득
```

```
기부가 없으면:
  L(1)이 실행 중인데 M(32)이 생성되면
  M이 L을 선점 -> L은 Lock A를 풀 수 없음 -> H(63)는 영원히 대기

기부가 있으면:
  L이 63으로 승격 -> M(32)보다 높음 -> L이 먼저 실행
  -> L이 Lock A 해제 -> H 실행 가능
```

### 중첩 기부 (Nested Donation)

```mermaid
graph LR
    H["H(63)"] -->|대기| LB["Lock B"]
    LB -->|보유| M["M(32)"]
    M -->|대기| LA["Lock A"]
    LA -->|보유| L["L(1)"]

```

```
donate_priority() 동작:

  curr = H
  lock = Lock B
  depth = 0

  반복 1: Lock B의 holder = M
          M.priority = max(32, 63) = 63
          curr = M, lock = M.wait_on_lock = Lock A
          depth = 1

  반복 2: Lock A의 holder = L
          L.priority = max(1, 63) = 63
          curr = L, lock = L.wait_on_lock = NULL
          depth = 2

  반복 종료 (lock == NULL)

  결과: L(1->63), M(32->63), H(63, BLOCKED)
```

---

## MLFQS: 우선순위 자동 계산 (Phase 4)

Phase 2-3에서는 프로그래머가 우선순위를 직접 지정했다.
Phase 4에서는 스케줄러가 CPU 사용량을 기반으로 자동 계산한다.

```mermaid
graph TD
    TICK["매 틱:<br/>현재 스레드의 recent_cpu += 1"]
    SEC["매 초"]
    LAVG["load_avg = (59/60) * load_avg<br/>+ (1/60) * ready_threads"]
    RCPU["모든 스레드에 대해:<br/>coeff = (2 * load_avg) / (2 * load_avg + 1)<br/>recent_cpu = coeff * recent_cpu + nice"]
    FOUR["매 4틱"]
    PRI["모든 스레드에 대해:<br/>priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)"]
    CLAMP["값 범위: 0~63 클램핑"]
    RESORT["ready_list 재정렬"]
    PREEMPT["현재 스레드보다 높은<br/>우선순위가 있으면 선점"]

    TICK
    SEC --> LAVG --> RCPU
    FOUR --> PRI --> CLAMP
    PRI --> RESORT
    PRI --> PREEMPT

```

```mermaid
graph LR
    HIGH_NICE["nice가 높은 스레드<br/>(양보적)"] --> LOW_PRI["priority가 내려감"] --> LESS_CPU["CPU를 덜 받음"]
    LOW_NICE["nice가 낮은 스레드<br/>(공격적)"] --> HIGH_PRI["priority가 올라감"] --> MORE_CPU["CPU를 더 받음"]
```

### MLFQS 모드에서 비활성화되는 것

```
thread_set_priority()  --> 무시 (스케줄러가 계산)
thread_get_priority()  --> 스케줄러가 계산한 값 반환
priority donation      --> 동작하지 않음
thread_create()의 priority 인자 --> 무시
```

---

## 수정하는 파일과 Phase의 관계

```mermaid
classDiagram
    class thread {
        tid_t tid
        enum thread_status status
        char name[16]
        int priority
        struct list_elem elem
        struct intr_frame tf
        unsigned magic
    }

    class Phase1_추가 {
        int64_t wake_tick
    }

    class Phase3_추가 {
        int original_priority
        struct lock *wait_on_lock
        struct list donations
        struct list_elem donation_elem
    }

    class Phase4_추가 {
        int nice
        int recent_cpu (fixed-point)
    }

    thread <|-- Phase1_추가
    thread <|-- Phase3_추가
    thread <|-- Phase4_추가
```

| 파일 | 함수 | 수정 Phase |
|------|------|-----------|
| `devices/timer.c` | `timer_sleep()` | Phase 1 |
| `devices/timer.c` | `timer_interrupt()` | Phase 1, 4 |
| `threads/thread.c` | `thread_create()` | Phase 2 |
| `threads/thread.c` | `thread_unblock()` | Phase 2 |
| `threads/thread.c` | `thread_yield()` | Phase 2 |
| `threads/thread.c` | `thread_set_priority()` | Phase 2, 3, 4 |
| `threads/thread.c` | `init_thread()` | Phase 1, 3, 4 |
| `threads/thread.c` | `next_thread_to_run()` | Phase 2 확인 |
| `threads/thread.c` | `thread_awake()` | Phase 1 추가 |
| `threads/thread.c` | `mlfqs_recalc_priority()` | Phase 4 추가 |
| `threads/thread.c` | `mlfqs_recalc_recent_cpu()` | Phase 4 추가 |
| `threads/thread.c` | `mlfqs_recalc_load_avg()` | Phase 4 추가 |
| `threads/thread.c` | `mlfqs_increment_recent_cpu()` | Phase 4 추가 |
| `threads/synch.c` | `sema_down()` | Phase 2 |
| `threads/synch.c` | `sema_up()` | Phase 2 |
| `threads/synch.c` | `lock_acquire()` | Phase 3 |
| `threads/synch.c` | `lock_release()` | Phase 3 |
| `threads/synch.c` | `cond_signal()` | Phase 2 |
| `threads/fixed_point.h` | (신규 파일) | Phase 4 |

---

## 테스트 통과 순서

각 Phase를 완료하면 아래 테스트가 순서대로 통과해야 한다.

```
Phase 1 완료 후:
  alarm-single .............. PASS
  alarm-multiple ............ PASS
  alarm-simultaneous ........ PASS
  alarm-negative ............ PASS
  alarm-zero ................ PASS

Phase 2 완료 후 (위 + 아래):
  priority-change ........... PASS
  priority-preempt .......... PASS
  priority-fifo ............. PASS
  priority-sema ............. PASS
  priority-condvar .......... PASS
  alarm-priority ............ PASS

Phase 3 완료 후 (위 + 아래):
  priority-donate-one ....... PASS
  priority-donate-multiple .. PASS
  priority-donate-multiple2 . PASS
  priority-donate-nest ...... PASS
  priority-donate-chain ..... PASS
  priority-donate-sema ...... PASS
  priority-donate-lower ..... PASS

Phase 4 완료 후 (위 + 아래):
  mlfqs-load-1 ............. PASS
  mlfqs-load-60 ............ PASS
  mlfqs-load-avg ........... PASS
  mlfqs-recent-1 ........... PASS
  mlfqs-fair-2 ............. PASS
  mlfqs-fair-20 ............ PASS
  mlfqs-nice-2 ............. PASS
  mlfqs-nice-10 ............ PASS
  mlfqs-block .............. PASS
```

Phase를 넘어갈 때 이전 Phase의 테스트가 깨지면 안 된다.
머지 담당자는 `make check` 전체 결과를 확인한 뒤 dev에 올린다.
