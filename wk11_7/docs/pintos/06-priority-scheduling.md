# Priority Scheduling 구현 가이드

## 개요

현재 Pintos 스케줄러는 **라운드로빈(FIFO)** 방식이다. 이를 **선점형 우선순위 스케줄링**으로 변경해야 한다.

### 우선순위 범위

```c
#define PRI_MIN      0    // 최저 우선순위
#define PRI_DEFAULT 31    // 기본 우선순위
#define PRI_MAX     63    // 최고 우선순위
```

---

## 요구사항 정리

### 기본 우선순위 스케줄링

1. **ready_list에서 가장 높은 우선순위 스레드**가 먼저 실행
2. 새 스레드가 현재 실행 중인 스레드보다 높은 우선순위를 가지면 **즉시 선점(preemption)**
3. 동기화 프리미티브(세마포어, 락, CV)에서 대기 중인 스레드 중 **가장 높은 우선순위**가 먼저 깨어남
4. `thread_set_priority()`로 우선순위를 낮추면 즉시 양보

### Priority Donation (우선순위 기부)

5. 높은 우선순위 스레드 H가 낮은 우선순위 스레드 L이 보유한 락을 기다릴 때, L에게 H의 우선순위를 **기부(donate)**
6. L이 락을 해제하면 기부된 우선순위 **회수**
7. **다중 기부(Multiple Donation)**: 하나의 스레드가 여러 락을 보유 → 여러 기부를 동시에 받을 수 있음
8. **중첩 기부(Nested Donation)**: H→M→L 체인에서 L도 H의 우선순위를 받음 (깊이 제한 8)
9. Priority donation은 **락(lock)에만** 적용. 세마포어/CV에는 적용하지 않음

---

## 핵심 개념: Priority Inversion (우선순위 역전)

```
시나리오:
  H(63) — 높은 우선순위
  M(32) — 중간 우선순위
  L(1)  — 낮은 우선순위

1. L이 Lock A를 획득하고 실행 중
2. H가 생성되어 L을 선점하고 실행
3. H가 Lock A를 요청 → L이 보유 중이므로 BLOCKED
4. L이 다시 실행되어야 하는데...
5. M이 생성되어 L을 선점 → M 실행
6. H는 L을 기다리고, L은 M에게 밀림 → H가 영원히 실행 불가!

해결: Priority Donation
  H가 Lock A를 요청할 때 L에게 자신의 우선순위(63)를 기부
  → L이 63으로 승격되어 M보다 먼저 실행
  → L이 Lock A 해제 → H 실행 가능
```

---

## struct thread 확장

```c
struct thread {
    /* 기존 필드 */
    tid_t tid;
    enum thread_status status;
    char name[16];
    int priority;                    // 기부 포함 실효 우선순위
    struct list_elem elem;

    /* === Priority Donation을 위해 추가 === */
    int original_priority;           // 기부 이전 원래 우선순위
    struct lock *wait_on_lock;       // 이 스레드가 대기 중인 락
    struct list donations;           // 이 스레드에게 기부한 스레드 목록
    struct list_elem donation_elem;  // donations 리스트용 elem

    /* === Alarm Clock === */
    int64_t wake_tick;

    /* 기존 필드 계속 */
    struct intr_frame tf;
    unsigned magic;
};
```

---

## 구현 단계

### 1단계: ready_list를 우선순위 순으로 관리

현재 `thread_unblock()`과 `thread_yield()`는 `list_push_back()`으로 FIFO 삽입한다. 이를 **우선순위 내림차순 정렬 삽입**으로 변경한다.

```c
/* thread.c — thread_unblock() 수정 */
void thread_unblock(struct thread *t) {
    enum intr_level old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    // 변경: push_back → insert_ordered
    list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
    t->status = THREAD_READY;
    intr_set_level(old_level);
}

/* 비교 함수 */
bool cmp_priority(const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->priority > tb->priority;
}
```

`thread_yield()`도 동일하게 `list_insert_ordered` 사용.

### 2단계: 선점 체크 포인트 추가

새 스레드가 현재 스레드보다 우선순위가 높으면 양보해야 하는 시점:

**a) thread_create() 이후:**
```c
tid_t thread_create(...) {
    // ... 기존 코드 ...
    thread_unblock(t);

    // 추가: 새 스레드의 우선순위가 더 높으면 양보
    if (t->priority > thread_current()->priority)
        thread_yield();

    return tid;
}
```

**b) thread_set_priority() 호출 시:**
```c
void thread_set_priority(int new_priority) {
    thread_current()->priority = new_priority;

    // 추가: ready_list의 최고 우선순위와 비교
    if (!list_empty(&ready_list)) {
        struct thread *top = list_entry(list_front(&ready_list),
                                        struct thread, elem);
        if (top->priority > new_priority)
            thread_yield();
    }
}
```

**c) sema_up() 이후:**
```c
void sema_up(struct semaphore *sema) {
    enum intr_level old_level = intr_disable();
    if (!list_empty(&sema->waiters)) {
        // 변경: 가장 높은 우선순위 스레드를 깨움
        list_sort(&sema->waiters, cmp_priority, NULL);
        thread_unblock(list_entry(list_pop_front(&sema->waiters),
                                  struct thread, elem));
    }
    sema->value++;

    // 추가: 선점 체크
    test_max_priority();

    intr_set_level(old_level);
}
```

### 3단계: sema_down()에서 우선순위 순 대기

```c
void sema_down(struct semaphore *sema) {
    // ...
    while (sema->value == 0) {
        // 변경: push_back → insert_ordered
        list_insert_ordered(&sema->waiters, &thread_current()->elem,
                           cmp_priority, NULL);
        thread_block();
    }
    // ...
}
```

### 4단계: condition variable에서 우선순위 순 signal

```c
void cond_signal(struct condition *cond, struct lock *lock) {
    if (!list_empty(&cond->waiters)) {
        // 변경: waiters 리스트를 우선순위 순 정렬 후 pop
        list_sort(&cond->waiters, cmp_sema_priority, NULL);
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
    }
}

/* semaphore_elem의 waiter 중 최고 우선순위로 비교 */
bool cmp_sema_priority(const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED) {
    struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

    struct list *la = &sa->semaphore.waiters;
    struct list *lb = &sb->semaphore.waiters;

    struct thread *ta = list_entry(list_front(la), struct thread, elem);
    struct thread *tb = list_entry(list_front(lb), struct thread, elem);

    return ta->priority > tb->priority;
}
```

---

## Priority Donation 구현

### lock_acquire() 수정

```c
void lock_acquire(struct lock *lock) {
    struct thread *curr = thread_current();

    if (lock->holder != NULL) {
        // 현재 스레드가 이 락을 기다린다고 기록
        curr->wait_on_lock = lock;
        // 락 보유자에게 우선순위 기부
        donate_priority();
    }

    sema_down(&lock->semaphore);

    // 락 획득 완료
    curr->wait_on_lock = NULL;
    lock->holder = curr;
}
```

### donate_priority() — 중첩 기부 처리

```c
void donate_priority(void) {
    struct thread *curr = thread_current();
    struct lock *lock = curr->wait_on_lock;
    int depth = 0;

    while (lock != NULL && depth < 8) {
        if (lock->holder == NULL) break;
        if (lock->holder->priority >= curr->priority) break;

        lock->holder->priority = curr->priority;
        curr = lock->holder;
        lock = curr->wait_on_lock;
        depth++;
    }
}
```

### lock_release() 수정

```c
void lock_release(struct lock *lock) {
    lock->holder = NULL;

    // 기부 목록에서 이 락 관련 기부 제거
    remove_with_lock(lock);
    // 남은 기부 중 최고 우선순위로 복원
    refresh_priority();

    sema_up(&lock->semaphore);
}
```

### refresh_priority()

```c
void refresh_priority(void) {
    struct thread *curr = thread_current();
    curr->priority = curr->original_priority;

    if (!list_empty(&curr->donations)) {
        list_sort(&curr->donations, cmp_priority_donation, NULL);
        struct thread *top = list_entry(list_front(&curr->donations),
                                        struct thread, donation_elem);
        if (top->priority > curr->priority)
            curr->priority = top->priority;
    }
}
```

### thread_set_priority() 수정 (donation 고려)

```c
void thread_set_priority(int new_priority) {
    struct thread *curr = thread_current();
    curr->original_priority = new_priority;
    refresh_priority();  // 기부가 있으면 기부값이 유지됨

    // 선점 체크
    test_max_priority();
}
```

---

## 중첩 기부(Nested Donation) 시나리오

```
스레드 H(63) → Lock B 요청 → 스레드 M(32) 보유 중
                              M → Lock A 요청 → 스레드 L(1) 보유 중

donate_priority() 동작:
  1. H가 M에게 63 기부 (M: 32 → 63)
  2. M이 Lock A를 기다리므로, M을 따라가서 L에게도 63 기부 (L: 1 → 63)
  깊이 제한: 8단계까지
```

---

## 다중 기부(Multiple Donation) 시나리오

```
스레드 L이 Lock A와 Lock B를 모두 보유
스레드 M(32)이 Lock A 대기 → L에게 32 기부
스레드 H(63)이 Lock B 대기 → L에게 63 기부

L의 실효 우선순위 = max(원래, 32, 63) = 63

L이 Lock B 해제 → H의 기부 제거 → L의 우선순위 = max(원래, 32)
L이 Lock A 해제 → M의 기부 제거 → L의 우선순위 = 원래값
```

---

## 관련 테스트

```
priority-change         — set_priority 후 양보
priority-preempt        — 높은 우선순위 스레드 생성 시 선점
priority-fifo           — 동일 우선순위에서 FIFO
priority-sema           — 세마포어에서 우선순위 순 깨우기
priority-condvar        — CV에서 우선순위 순 signal
priority-donate-one     — 단일 기부
priority-donate-multiple — 다중 기부
priority-donate-multiple2 — 다중 기부 변형
priority-donate-nest    — 중첩 기부
priority-donate-chain   — 기부 체인
priority-donate-sema    — 세마포어와 기부 조합
priority-donate-lower   — 기부 중 set_priority
```

---

## 수정 대상 파일 요약

| 파일 | 수정 내용 |
|------|----------|
| `include/threads/thread.h` | `original_priority`, `wait_on_lock`, `donations`, `donation_elem` 추가 |
| `threads/thread.c` | `thread_create`, `thread_set_priority`, `thread_unblock`, `thread_yield`, `init_thread` 수정 |
| `threads/synch.c` | `sema_down`, `sema_up`, `lock_acquire`, `lock_release`, `cond_signal` 수정 |
