# Pintos 자료구조 핵심 정리

## Pintos Doubly-Linked List

Pintos에서 가장 많이 사용하는 자료구조다. `ready_list`, `sleep_list`, `waiters` 등 거의 모든 곳에서 활용된다. 반드시 완벽히 이해해야 한다.

### 구조 정의

```c
/* lib/kernel/list.h */

/* 리스트 원소 — 실제 데이터 구조체 안에 임베딩 */
struct list_elem {
    struct list_elem *prev;
    struct list_elem *next;
};

/* 리스트 — head와 tail을 센티널(sentinel)로 사용 */
struct list {
    struct list_elem head;  // 더미 헤드
    struct list_elem tail;  // 더미 테일
};
```

### 핵심 특성: Intrusive List

일반적인 연결 리스트와 달리 Pintos의 리스트는 **침투적(intrusive)** 방식이다. 노드가 데이터를 감싸는 것이 아니라, **데이터 구조체 안에 `list_elem`이 포함**된다.

```c
struct thread {
    tid_t tid;
    int priority;
    struct list_elem elem;  // ← 리스트 노드가 구조체 안에 포함
    // ...
};
```

이 방식의 장점은 별도의 노드 할당이 필요 없다는 것이다.

---

## list_entry 매크로 — 가장 중요한 개념

`list_elem` 포인터로부터 그것을 포함하는 **부모 구조체의 포인터**를 역산한다.

```c
#define list_entry(LIST_ELEM, STRUCT, MEMBER)                   \
    ((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next                 \
                 - offsetof(STRUCT, MEMBER.next)))
```

### offsetof 매크로

```c
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *) 0)->MEMBER)
```

구조체 시작 주소로부터 특정 멤버까지의 **바이트 오프셋**을 계산한다.

### 동작 원리 — 메모리 레이아웃으로 이해

```
struct thread 메모리 레이아웃 (낮은 주소 → 높은 주소):

주소        필드
─────────────────────
0x1000     tid          (offset 0)
0x1004     status       (offset 4)
0x1008     name[16]     (offset 8)
0x1018     priority     (offset 24)
0x101C     elem.prev    (offset 28)  ← list_elem 시작
0x1024     elem.next    (offset 36)
0x102C     ...          이하 필드들
```

`list_entry(e, struct thread, elem)` 의 동작:

```
1. e = &elem = 0x101C (list_elem의 주소)
2. offsetof(struct thread, elem) = 28
3. (uint8_t *)e - 28 = 0x101C - 28 = 0x1000
4. (struct thread *)0x1000 → thread 구조체의 시작 주소!
```

### 사용 예시

```c
// ready_list에서 스레드를 꺼내기
struct list_elem *e = list_pop_front(&ready_list);
struct thread *t = list_entry(e, struct thread, elem);
// 이제 t->priority, t->tid 등에 접근 가능
```

```c
// 리스트 순회
struct list_elem *e;
for (e = list_begin(&ready_list);
     e != list_end(&ready_list);
     e = list_next(e)) {
    struct thread *t = list_entry(e, struct thread, elem);
    printf("Thread: %s, priority: %d\n", t->name, t->priority);
}
```

---

## 주요 리스트 API

### 초기화/판별

```c
void list_init(struct list *);           // 초기화
bool list_empty(const struct list *);    // 비어있는지
size_t list_size(const struct list *);   // 원소 수 (O(n))
```

### 삽입

```c
void list_push_front(struct list *, struct list_elem *);  // 앞에 삽입
void list_push_back(struct list *, struct list_elem *);   // 뒤에 삽입
void list_insert(struct list_elem *before, struct list_elem *);  // before 앞에 삽입

// 정렬 삽입 — Project 1에서 핵심
void list_insert_ordered(struct list *, struct list_elem *,
                         list_less_func *, void *aux);
```

### 제거

```c
struct list_elem *list_remove(struct list_elem *);        // 제거 (다음 원소 반환)
struct list_elem *list_pop_front(struct list *);          // 앞에서 제거+반환
struct list_elem *list_pop_back(struct list *);           // 뒤에서 제거+반환
```

### 순회

```c
struct list_elem *list_begin(const struct list *);   // 첫 원소
struct list_elem *list_end(const struct list *);     // tail (sentinel)
struct list_elem *list_next(struct list_elem *);     // 다음
struct list_elem *list_prev(struct list_elem *);     // 이전
struct list_elem *list_front(const struct list *);   // 첫 원소 (제거 안 함)
struct list_elem *list_back(const struct list *);    // 마지막 원소 (제거 안 함)
```

### 정렬/검색

```c
void list_sort(struct list *, list_less_func *, void *aux);
// 머지소트, O(n log n)

struct list_elem *list_max(const struct list *, list_less_func *, void *aux);
struct list_elem *list_min(const struct list *, list_less_func *, void *aux);
```

### 비교 함수 시그니처

```c
typedef bool list_less_func(const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux);
// a가 b보다 "작으면" true 반환
```

---

## list_insert_ordered 사용 패턴

Project 1에서 가장 많이 쓰는 패턴이다.

```c
/* 우선순위 내림차순 (높은 우선순위가 앞) */
bool cmp_priority(const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux UNUSED) {
    return list_entry(a, struct thread, elem)->priority
         > list_entry(b, struct thread, elem)->priority;
}

// 사용
list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
```

```c
/* wake_tick 오름차순 (먼저 깨어날 스레드가 앞) */
bool cmp_wake_tick(const struct list_elem *a,
                   const struct list_elem *b,
                   void *aux UNUSED) {
    return list_entry(a, struct thread, elem)->wake_tick
         < list_entry(b, struct thread, elem)->wake_tick;
}

// 사용
list_insert_ordered(&sleep_list, &t->elem, cmp_wake_tick, NULL);
```

---

## elem의 이중 용도

`struct thread`의 `elem` 필드는 **한 번에 하나의 리스트에만** 속할 수 있다.

```
RUNNING 상태 → 어떤 리스트에도 없음
READY 상태   → ready_list에 있음
BLOCKED 상태 → semaphore의 waiters 또는 sleep_list에 있음
```

이것이 가능한 이유는 이 상태들이 **상호 배타적**이기 때문이다.

만약 하나의 스레드를 여러 리스트에 동시에 넣어야 한다면, `struct thread`에 추가 `list_elem`을 선언해야 한다. (예: `donation_elem`)

---

## 안전한 리스트 순회 중 제거

리스트를 순회하면서 원소를 제거할 때는 `list_remove()`의 반환값을 사용한다.

```c
struct list_elem *e = list_begin(&some_list);
while (e != list_end(&some_list)) {
    struct thread *t = list_entry(e, struct thread, elem);
    if (should_remove(t)) {
        e = list_remove(e);  // 다음 원소 포인터 반환
    } else {
        e = list_next(e);
    }
}
```

---

## 구조체 내 여러 list_elem 사용

Priority Donation 구현 시 하나의 스레드가 `ready_list`와 `donations` 리스트 양쪽에 속해야 할 수 있다.

```c
struct thread {
    struct list_elem elem;            // ready_list / sleep_list / waiters 용
    struct list_elem donation_elem;   // donations 리스트 전용
};
```

각 `list_elem`은 별도의 리스트에 독립적으로 삽입/제거할 수 있다.
