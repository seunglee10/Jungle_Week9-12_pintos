# 동기화 프리미티브 상세 분석

## 왜 동기화가 필요한가

Pintos 커널은 **단일 CPU** 환경이지만, 타이머 인터럽트에 의한 선점이 발생하므로 공유 데이터에 대한 동기화가 필수다.

두 가지 동시성 원인:
1. **스레드 간 선점**: 타이머 인터럽트로 실행 중인 스레드가 교체됨
2. **인터럽트 핸들러**: `timer_interrupt()` 같은 핸들러가 커널 스레드의 데이터를 접근

---

## 인터럽트 비활성화

### 언제 사용하는가

- 인터럽트 핸들러와 커널 스레드 간 공유 데이터 보호
- 인터럽트 핸들러에서는 lock/semaphore를 사용할 수 없음 (sleep 불가)
- MLFQS의 전역 변수(load_avg 등) 접근 시

### 패턴

```c
enum intr_level old_level = intr_disable();
// ... 임계 구역 ...
intr_set_level(old_level);
```

### 주의

- 인터럽트 비활성화 구간을 **최소화**해야 한다
- 길어지면 타이머 틱을 놓치거나 입력 이벤트를 잃을 수 있다
- 가능하면 세마포어/락을 우선 사용

---

## Semaphore (세마포어)

### 구조

```c
struct semaphore {
    unsigned value;        // 현재 값 (음이 아닌 정수)
    struct list waiters;   // 대기 중인 스레드 리스트
};
```

### 동작

#### sema_down (P 연산) — "자원 획득 요청"

```c
void sema_down(struct semaphore *sema) {
    enum intr_level old_level = intr_disable();
    while (sema->value == 0) {
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block();  // BLOCKED 상태로 전환
    }
    sema->value--;
    intr_set_level(old_level);
}
```

1. value가 0이면 현재 스레드를 waiters에 넣고 BLOCK
2. 다른 스레드가 sema_up하면 깨어나서 while 재검사
3. value > 0이면 감소시키고 진행

#### sema_up (V 연산) — "자원 반환"

```c
void sema_up(struct semaphore *sema) {
    enum intr_level old_level = intr_disable();
    if (!list_empty(&sema->waiters))
        thread_unblock(list_entry(list_pop_front(&sema->waiters),
                                  struct thread, elem));
    sema->value++;
    intr_set_level(old_level);
}
```

1. 대기 스레드가 있으면 하나를 READY로 전환
2. value 증가

### Project 1에서 수정할 부분

- `sema_down()`: waiters에 **우선순위 순 삽입** (list_insert_ordered)
- `sema_up()`: 깨울 때 **가장 높은 우선순위 스레드**를 깨움 (list_sort 후 pop_front)
- `sema_up()` 후 **선점 체크** 추가

---

## Lock (락)

### 구조

```c
struct lock {
    struct thread *holder;      // 락 보유 스레드
    struct semaphore semaphore; // value=1인 이진 세마포어
};
```

### 세마포어와의 차이

| 특성 | Semaphore | Lock |
|------|-----------|------|
| value 범위 | 0 이상 아무 값 | 0 또는 1 |
| 소유자 | 없음 | holder 추적 |
| acquire/release | 다른 스레드 가능 | **같은 스레드**만 |
| 재진입 | N/A | 불가 (recursive lock 아님) |

### 동작

```c
void lock_acquire(struct lock *lock) {
    sema_down(&lock->semaphore);    // value가 0이면 대기
    lock->holder = thread_current(); // 소유자 기록
}

void lock_release(struct lock *lock) {
    lock->holder = NULL;
    sema_up(&lock->semaphore);      // 대기 스레드 깨움
}
```

### Project 1에서 수정할 부분

- `lock_acquire()`: Priority Donation 처리 (기부 체인 따라가며 우선순위 전파)
- `lock_release()`: 기부 회수 + 우선순위 복원

---

## Condition Variable (조건 변수)

### 구조

```c
struct condition {
    struct list waiters;   // semaphore_elem 리스트
};

struct semaphore_elem {
    struct list_elem elem;
    struct semaphore semaphore;  // 각 대기 스레드마다 전용 세마포어
};
```

### Mesa-style 모니터

Pintos의 CV는 **Mesa 방식**이다. `cond_signal()`이 대기 스레드를 깨우더라도 즉시 실행을 보장하지 않으므로, 깨어난 후 반드시 조건을 **재검사**해야 한다.

```c
// 올바른 패턴
lock_acquire(&lock);
while (!condition)      // if가 아닌 while!
    cond_wait(&cond, &lock);
// ... 조건 충족 시 처리 ...
lock_release(&lock);
```

### 동작

```c
void cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;
    sema_init(&waiter.semaphore, 0);
    list_push_back(&cond->waiters, &waiter.elem);
    lock_release(lock);         // 락 해제 (다른 스레드가 signal 가능)
    sema_down(&waiter.semaphore); // 대기
    lock_acquire(lock);         // 깨어나면 락 재획득
}

void cond_signal(struct condition *cond, struct lock *lock) {
    if (!list_empty(&cond->waiters))
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
}

void cond_broadcast(struct condition *cond, struct lock *lock) {
    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}
```

### Project 1에서 수정할 부분

- `cond_signal()`: waiters를 **우선순위 순**으로 정렬하고 가장 높은 우선순위의 대기자를 깨움
- CV의 waiters는 `semaphore_elem`이므로, 비교 시 semaphore의 waiters 내 최고 우선순위 스레드를 기준으로 비교해야 함

---

## Optimization Barrier

```c
#define barrier() asm volatile ("" : : : "memory")
```

컴파일러가 메모리 접근 순서를 최적화로 바꾸는 것을 방지한다. `timer_ticks()` 등에서 사용된다.

```c
int64_t timer_ticks(void) {
    enum intr_level old_level = intr_disable();
    int64_t t = ticks;
    intr_set_level(old_level);
    barrier();  // 컴파일러가 t 읽기를 뒤로 미루지 못하게
    return t;
}
```

---

## 동기화 전략 요약

| 상황 | 권장 도구 |
|------|----------|
| 스레드 간 상호 배제 | Lock |
| 생산자-소비자 패턴 | Semaphore |
| 조건 대기 | Condition Variable + Lock |
| 인터럽트 핸들러와 공유 데이터 | intr_disable/enable |
| 컴파일러 최적화 방지 | barrier() |

---

## 경쟁 조건(Race Condition) 방지 체크리스트

1. 전역/공유 변수를 접근하는 모든 코드에 동기화 적용했는가?
2. ready_list, sleep_list 접근 시 인터럽트를 비활성화했는가?
3. lock_acquire/release 사이에서만 공유 데이터를 수정하는가?
4. sema_down 후 조건을 while로 재검사하는가?
5. 인터럽트 핸들러에서 sleep 가능한 함수를 호출하지 않는가?
