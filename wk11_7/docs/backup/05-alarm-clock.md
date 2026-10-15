# Alarm Clock 구현 가이드

## 문제 정의

현재 `timer_sleep()` 은 **busy-wait** 방식으로 구현되어 있다.

```c
/* 현재 코드 — devices/timer.c */
void timer_sleep(int64_t ticks) {
    int64_t start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);
    while (timer_elapsed(start) < ticks)
        thread_yield();  // ← CPU를 낭비하며 반복 양보
}
```

이 방식은 sleep 중인 스레드가 계속 ready_list에 들어가 스케줄링되기 때문에 CPU 사이클을 낭비한다. 이를 **블로킹 방식**으로 개선해야 한다.

---

## 목표

- `timer_sleep(ticks)` 호출 시 스레드를 **BLOCKED 상태**로 전환
- 지정된 틱 수가 지나면 자동으로 **READY 상태**로 복귀
- busy-wait를 완전히 제거

---

## 핵심 개념

### timer tick

- 시스템 타이머가 `TIMER_FREQ`(기본 100) 만큼 초당 인터럽트 발생
- 1 tick = 10ms (기본값)
- `timer_ticks()`: 부팅 이후 경과한 총 틱 수 반환
- `timer_elapsed(then)`: `then` 시점 이후 경과 틱 수

### thread_block() vs thread_yield()

| | thread_block() | thread_yield() |
|---|---|---|
| 상태 변화 | RUNNING → BLOCKED | RUNNING → READY |
| 재스케줄링 | `thread_unblock()` 호출 필요 | 즉시 ready_list로 복귀 |
| 인터럽트 | 반드시 OFF 상태에서 호출 | 내부에서 OFF로 전환 |

---

## 구현 전략

### 1단계: sleep list 추가

`thread.c` 또는 `timer.c`에 sleep 중인 스레드를 관리하는 리스트를 추가한다.

```c
/* timer.c 또는 thread.c */
static struct list sleep_list;  // sleep 중인 스레드 리스트
```

### 2단계: struct thread에 wake_tick 필드 추가

```c
/* include/threads/thread.h — struct thread에 추가 */
int64_t wake_tick;  // 깨어나야 할 시각 (ticks)
```

### 3단계: timer_sleep() 수정

```c
void timer_sleep(int64_t ticks) {
    int64_t start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);

    if (ticks <= 0) return;

    enum intr_level old_level = intr_disable();

    struct thread *curr = thread_current();
    curr->wake_tick = start + ticks;

    // wake_tick 기준 오름차순 정렬 삽입 (선택)
    list_insert_ordered(&sleep_list, &curr->elem, compare_wake_tick, NULL);
    // 또는 단순히 list_push_back(&sleep_list, &curr->elem);

    thread_block();  // BLOCKED 상태로 전환

    intr_set_level(old_level);
}
```

### 4단계: timer_interrupt()에서 깨우기

```c
static void timer_interrupt(struct intr_frame *args UNUSED) {
    ticks++;
    thread_tick();

    // sleep_list에서 깨울 스레드 확인
    thread_awake(ticks);  // 별도 함수로 분리
}
```

```c
void thread_awake(int64_t current_ticks) {
    struct list_elem *e = list_begin(&sleep_list);

    while (e != list_end(&sleep_list)) {
        struct thread *t = list_entry(e, struct thread, elem);

        if (t->wake_tick <= current_ticks) {
            e = list_remove(e);     // 리스트에서 제거 (다음 원소 반환)
            thread_unblock(t);      // READY 상태로 전환
        } else {
            // 정렬된 경우 여기서 break 가능
            e = list_next(e);
        }
    }
}
```

### 5단계: 초기화

```c
/* timer_init() 또는 thread_init()에서 */
list_init(&sleep_list);
```

---

## 정렬 삽입 시 비교 함수

sleep_list를 wake_tick 오름차순으로 정렬하면 `timer_interrupt()`에서 앞쪽만 확인하고 바로 break할 수 있어 효율적이다.

```c
bool compare_wake_tick(const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->wake_tick < tb->wake_tick;
}
```

---

## 주의사항

1. **timer_interrupt()는 인터럽트 컨텍스트**에서 실행된다.
   - `thread_unblock()`은 인터럽트 컨텍스트에서 호출 가능 (내부에서 intr_disable 처리)
   - 단, `thread_block()`이나 `sema_down()`은 불가

2. **elem의 이중 용도**: `struct thread`의 `elem`은 ready_list와 sleep_list에서 동시에 사용될 수 없다. BLOCKED 상태일 때만 sleep_list에 넣으므로 ready_list와 충돌하지 않는다.

3. **인터럽트 비활성화 최소화**: `timer_sleep()`에서 `intr_disable()` → `thread_block()` → (다른 스레드에서 복귀 후) `intr_set_level()` 패턴을 사용한다.

---

## 관련 테스트

```
alarm-single          — 기본 단일 sleep
alarm-multiple        — 여러 스레드 동시 sleep
alarm-simultaneous    — 동일 시간에 여러 스레드 깨우기
alarm-negative        — 음수/0 틱
alarm-zero            — 0 틱 sleep
alarm-priority        — sleep 후 우선순위 순서대로 깨우기 (Priority Scheduling 후)
```

테스트 실행:
```bash
cd threads/build
pintos -- -q run alarm-multiple
# 또는
make check
```

---

## 수정 대상 파일 요약

| 파일 | 수정 내용 |
|------|----------|
| `include/threads/thread.h` | `struct thread`에 `wake_tick` 필드 추가 |
| `devices/timer.c` | `timer_sleep()` 재구현, `timer_interrupt()`에 깨우기 로직 |
| `threads/thread.c` | `sleep_list` 관리 함수 (선택적 위치) |
