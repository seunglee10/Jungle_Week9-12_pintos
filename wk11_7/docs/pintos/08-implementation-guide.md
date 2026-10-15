# 구현 순서 및 전략 가이드

## 권장 구현 순서

```
Phase 1: Alarm Clock (1일)
    ↓
Phase 2: Priority Scheduling 기본 (1일)
    ↓
Phase 3: Priority Donation (2일)
    ↓
Phase 4: Advanced Scheduler — MLFQS (1~2일)
```

---

## Phase 1: Alarm Clock

### 작업 목록

1. `struct thread`에 `wake_tick` 필드 추가
2. `sleep_list` 전역 리스트 선언 및 초기화
3. `timer_sleep()` 재구현 (busy-wait → thread_block)
4. `timer_interrupt()`에서 깨우기 로직 추가
5. 테스트: `alarm-single`, `alarm-multiple`, `alarm-simultaneous`

### 체크포인트

- [ ] busy-wait 코드 완전 제거
- [ ] idle 스레드의 CPU 점유율이 대폭 감소
- [ ] 음수/0 틱에 대한 예외 처리

---

## Phase 2: Priority Scheduling 기본

### 작업 목록

1. `cmp_priority()` 비교 함수 작성
2. `thread_unblock()` — `list_push_back` → `list_insert_ordered`
3. `thread_yield()` — 동일하게 정렬 삽입
4. `thread_create()` — unblock 후 선점 체크
5. `thread_set_priority()` — 선점 체크
6. `sema_down()` — waiters에 우선순위 순 삽입
7. `sema_up()` — 가장 높은 우선순위 깨움 + 선점 체크
8. `cond_signal()` — 우선순위 순 signal
9. `next_thread_to_run()` — 이미 정렬되어 있으면 그대로 유지

### 체크포인트

- [ ] `priority-change` 통과
- [ ] `priority-preempt` 통과
- [ ] `priority-fifo` 통과
- [ ] `priority-sema` 통과
- [ ] `priority-condvar` 통과

---

## Phase 3: Priority Donation

### 작업 목록

1. `struct thread`에 필드 추가: `original_priority`, `wait_on_lock`, `donations`, `donation_elem`
2. `init_thread()`에서 새 필드 초기화
3. `lock_acquire()` 수정 — 기부 로직
4. `lock_release()` 수정 — 기부 회수 + 우선순위 복원
5. `donate_priority()` — 중첩 기부 (깊이 8)
6. `remove_with_lock()` — 특정 락 관련 기부 제거
7. `refresh_priority()` — 남은 기부 중 최대값으로 복원
8. `thread_set_priority()` — original_priority 기반으로 수정

### 체크포인트

- [ ] `priority-donate-one` 통과
- [ ] `priority-donate-multiple` 통과
- [ ] `priority-donate-nest` 통과
- [ ] `priority-donate-chain` 통과
- [ ] `priority-donate-sema` 통과
- [ ] `priority-donate-lower` 통과

### 디버깅 팁

- 기부 관련 버그는 대부분 **기부 회수 누락**에서 발생
- 중첩 기부에서 무한 루프가 발생하면 **깊이 제한(8)** 확인
- `thread_get_priority()`가 기부 포함 값을 반환하는지 확인

---

## Phase 4: MLFQS

### 작업 목록

1. `fixed_point.h` 매크로 파일 작성
2. `struct thread`에 `nice`, `recent_cpu` 필드 추가
3. `load_avg` 전역 변수 추가
4. `thread_set_nice()`, `thread_get_nice()` 구현
5. `thread_get_load_avg()`, `thread_get_recent_cpu()` 구현
6. `timer_interrupt()`에 MLFQS 업데이트 로직 추가
7. `thread_set_priority()`에 MLFQS 모드 가드 추가
8. `lock_acquire/release`에 MLFQS 모드 가드 추가 (donation 비활성화)

### 체크포인트

- [ ] `mlfqs-load-1` 통과
- [ ] `mlfqs-load-60` 통과
- [ ] `mlfqs-recent-1` 통과
- [ ] `mlfqs-fair-*` 통과
- [ ] `mlfqs-nice-*` 통과
- [ ] 기존 alarm, priority 테스트 여전히 통과

---

## 코딩 스타일

Pintos는 C 프로젝트이므로 `docs/convention/c-style.md`를 따른다.
핵심 규칙만 요약하면 다음과 같다.

- 들여쓰기: 4칸 스페이스 (탭 금지)
- 네이밍: snake_case (변수, 함수), UPPER_SNAKE_CASE (상수, 매크로)
- 포인터: `int *ptr` (변수에 붙인다)
- 중괄호: 함수 정의만 다음 줄, 나머지(if/for/while)는 같은 줄
- 단일 문장 블록도 중괄호 필수
- 조건문: 변수를 왼쪽, 상수를 오른쪽 (`if (ptr == NULL)`)
- 함수 50줄 이내, 파라미터 4개 이하 권장
- 파일 외부에 노출 불필요한 함수/변수는 `static`
- malloc 반환값 NULL 체크, free 후 NULL 대입
- 주석은 "왜(why)"를 적는다

### 커밋 메시지

`docs/convention/commit-convention.md`를 따른다.

```
<type>: <한국어 제목>

예시:
feat: Alarm Clock sleep_list 방식으로 구현
fix: sema_up에서 우선순위 비교 조건 오류를 수정
refactor: donate_priority 중첩 루프를 단순화
```

---

## 코드 작성 가이드라인

### 인터럽트 관리 원칙

```c
// 좋은 패턴: 필요한 구간만 비활성화
enum intr_level old_level = intr_disable();
// 최소한의 코드
intr_set_level(old_level);

// 나쁜 패턴: 광범위한 비활성화
intr_disable();
// ... 수십 줄의 코드 ...
intr_enable();
```

### 어설션 활용

```c
ASSERT(is_thread(t));
ASSERT(t->status == THREAD_BLOCKED);
ASSERT(intr_get_level() == INTR_OFF);
ASSERT(!intr_context());  // 인터럽트 핸들러가 아닌지
```

### 디버깅 출력

개발 중에는 `printf`로 디버깅하되, 제출 전 반드시 제거한다.

```c
#ifdef DEBUG
printf("Thread %s: priority %d → %d\n",
       t->name, old_pri, t->priority);
#endif
```

---

## 빌드 및 테스트

### 빌드

```bash
cd pintos/threads
make clean && make
```

### 개별 테스트 실행

```bash
cd build
# 방법 1: pintos 직접 실행
pintos -- -q run alarm-multiple

# 방법 2: make로 특정 테스트
make tests/threads/alarm-multiple.result

# MLFQS 테스트 (커널 옵션 자동 전달)
make tests/threads/mlfqs-load-1.result
```

### 전체 테스트

```bash
make check
# 또는
make grade
```

### 테스트 결과 확인

```bash
cat tests/threads/alarm-multiple.result
# PASS 또는 FAIL + 에러 메시지
```

---

## 디버깅 도구

### GDB 사용

```bash
pintos --gdb -- -q run alarm-multiple
# 다른 터미널에서
pintos-gdb build/kernel.o
(gdb) target remote localhost:1234
(gdb) break thread_create
(gdb) continue
```

### backtrace 분석

커널 패닉 시 backtrace가 출력된다:

```
Call stack: 0x800... 0x800... 0x800...
```

이를 해석하려면:

```bash
backtrace build/kernel.o 0x800... 0x800... 0x800...
```

---

## 흔한 실수와 해결

### 1. 스택 오버플로

**증상**: `ASSERT(is_thread(t))` 실패, `magic` 값 훼손

**원인**: 큰 배열을 스택에 할당

**해결**: `palloc_get_page()` 또는 `malloc()` 사용

### 2. 인터럽트 상태 불일치

**증상**: `ASSERT(intr_get_level() == INTR_OFF)` 실패

**원인**: `intr_set_level(old_level)` 누락

**해결**: 모든 `intr_disable()` 경로에 반드시 `intr_set_level()` 매칭

### 3. list_entry 잘못된 멤버 지정

**증상**: 엉뚱한 메모리 접근, segfault

**원인**: `list_entry(e, struct thread, elem)` 에서 `elem` 대신 다른 필드 사용

**해결**: 해당 `list_elem`이 어느 리스트의 어느 필드인지 정확히 매칭

### 4. elem 이중 사용

**증상**: 리스트 손상, 무한 루프

**원인**: 하나의 `elem`을 동시에 두 리스트에 넣음

**해결**: 스레드 상태(READY/BLOCKED)별로 elem이 하나의 리스트에만 속하는지 확인. 추가 리스트 필요 시 `donation_elem` 같은 별도 필드 사용

### 5. MLFQS 오버플로

**증상**: 테스트 값이 크게 벗어남

**원인**: fixed-point 곱셈에서 int32 오버플로

**해결**: `((int64_t) x) * y / F` 패턴 사용

---

## 팀 협업 전략

별도 문서 `09-team-strategy.md` 에 상세히 정리되어 있다.

핵심 방침: **각자 전부 구현하고, 머지 담당은 돌아가면서 맡는다.**
매일 코어타임에 4명의 코드를 비교하고, 머지 담당자가 기준 코드를 선택하여 master에 병합한다.

```
main                            (릴리즈, 테스트 통과 상태)
  +-- hotfix                    (버그 수정 전용)
  +-- dev                       (개발 머지용)
       +-- member/woonyong      (개인 브랜치)
       +-- member/teammate-b
       +-- member/teammate-c
       +-- member/teammate-d
```

---

## 수정 대상 파일 전체 요약

| 파일 | Alarm | Priority | Donation | MLFQS |
|------|:-----:|:--------:|:--------:|:-----:|
| `include/threads/thread.h` | ✅ | ✅ | ✅ | ✅ |
| `threads/thread.c` | ✅ | ✅ | ✅ | ✅ |
| `devices/timer.c` | ✅ | | | ✅ |
| `threads/synch.c` | | ✅ | ✅ | |
| `threads/fixed_point.h` (신규) | | | | ✅ |
