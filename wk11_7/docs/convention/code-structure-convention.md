---
name: code-structure-convention
description: C/Pintos 코드 구조 컨벤션. 함수 배치, 보조 함수 위치, 변수 범위, static 전역 변수 사용 기준을 다룬다.
---

# 코드 구조 컨벤션

> 목적: 파일의 실행 흐름을 해치지 않으면서 새 코드를 자연스러운 위치에 배치하고, 변수와 함수의 영향 범위를 작게 유지한다.

---

## 함수 배치

새로운 함수는 파일의 역할과 실행 흐름을 해치지 않는 위치에 추가한다.

공개 함수 또는 상위 수준의 흐름은 파일의 위쪽에 배치한다.

내부 구현 세부사항은 파일의 아래쪽에 배치한다.

```c
void timer_init (void);
void timer_sleep (int64_t ticks);

static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
```

---

## 보조 함수 위치

특정 함수와 밀접하게 관련된 보조 함수는 해당 함수 근처에 배치한다.

여러 함수에서 공유하는 내부 보조 함수는 파일 하단의 내부 구현 영역에 배치한다.

```c
static bool
wakeup_tick_less (const struct list_elem *lhs,
                  const struct list_elem *rhs,
                  void *aux UNUSED) {
	...
}

void
thread_sleep (int64_t wakeup_tick) {
	...
}
```

---

## 변수 범위

변수는 가능한 한 최소한의 범위에서 선언한다.

특정 블록에서만 필요한 변수는 해당 블록 안에서 선언한다.

```c
while (!list_empty (&sleep_list)) {
	struct list_elem *front = list_front (&sleep_list);
	struct thread *sleeper = list_entry (front, struct thread, elem);
	...
}
```

---

## 전역 변수

파일 안에서만 필요한 전역 변수는 `static`으로 선언한다.

여러 파일에서 공유해야 하는 경우에만 일반 전역 변수를 사용한다.

```c
static struct list sleep_list;
static int64_t next_wakeup_tick;
```

---

## 금지 사항

- 파일 역할과 무관한 함수를 임의 위치에 추가하지 않는다.
- 공개 함수 사이에 관련 없는 내부 구현 함수를 끼워 넣지 않는다.
- 함수 전체에서 쓰지 않는 변수를 함수 시작부에 미리 선언하지 않는다.
- 파일 내부에서만 쓰는 전역 변수를 `static` 없이 선언하지 않는다.
