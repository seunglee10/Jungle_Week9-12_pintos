# 개발 환경 및 디버깅 가이드

## 컨테이너 실행 (CLion + Docker DevContainer)

CLion에서 프로젝트를 열면 `.devcontainer/Dockerfile` 기반으로 Ubuntu 22.04 컨테이너가 생성된다.
터미널을 열면 컨테이너 내부 쉘이 뜬다.

### PATH 설정 (최초 1회)

컨테이너 터미널에서 아래를 실행한다. 한 번만 하면 이후 새 터미널에서 자동 적용된다.

```bash
echo 'source /IdeaProjects/SW_AI-W09-pintos/pintos/activate' >> ~/.bashrc
source ~/.bashrc
```

이 명령은 `pintos/utils/` 디렉터리를 PATH에 추가하여 `pintos` 명령어를 사용할 수 있게 한다.

설정 안 하면 매번 터미널 열 때마다 아래를 수동으로 실행해야 한다.

```bash
source /IdeaProjects/SW_AI-W09-pintos/pintos/activate
```

---

## 일상적인 개발 흐름

개발 시간의 90%는 이 루프를 반복한다.

```bash
# 1. 코드 수정 (CLion 에디터에서)

# 2. 빌드
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads
make

# 3. 단일 테스트 실행
cd build
pintos -- -q run alarm-multiple

# 4. 출력 확인 후 코드 수정 -> 2번으로 돌아감
```

빌드 에러가 나면 `make` 출력의 에러 메시지를 읽고 코드를 수정한다.
실행 결과가 이상하면 코드를 수정하고 다시 `make` -> 실행.

### 전체 테스트

```bash
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads/build
make check
```

모든 테스트를 돌리고 PASS/FAIL을 출력한다.
시간이 오래 걸리므로 (수 분) 개별 테스트로 먼저 확인하고, 어느 정도 완성되면 전체 테스트를 돌린다.

### 개별 테스트 목록

```bash
# Alarm Clock
pintos -- -q run alarm-single
pintos -- -q run alarm-multiple
pintos -- -q run alarm-simultaneous
pintos -- -q run alarm-zero
pintos -- -q run alarm-negative
pintos -- -q run alarm-priority

# Priority Scheduling
pintos -- -q run priority-change
pintos -- -q run priority-preempt
pintos -- -q run priority-fifo
pintos -- -q run priority-sema
pintos -- -q run priority-condvar

# Priority Donation
pintos -- -q run priority-donate-one
pintos -- -q run priority-donate-multiple
pintos -- -q run priority-donate-multiple2
pintos -- -q run priority-donate-nest
pintos -- -q run priority-donate-chain
pintos -- -q run priority-donate-sema
pintos -- -q run priority-donate-lower

# MLFQS (자동으로 -mlfqs 옵션 적용)
pintos -- -q -mlfqs run mlfqs-load-1
pintos -- -q -mlfqs run mlfqs-load-60
pintos -- -q -mlfqs run mlfqs-recent-1
pintos -- -q -mlfqs run mlfqs-fair-2
pintos -- -q -mlfqs run mlfqs-nice-2
pintos -- -q -mlfqs run mlfqs-nice-10
pintos -- -q -mlfqs run mlfqs-block
```

### 클린 빌드

빌드가 꼬였을 때 처음부터 다시 빌드한다.

```bash
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads
make clean
make
```

---

## printf 디버깅

가장 간단하고 빠른 디버깅 방법이다.
코드에 `printf`를 넣어서 변수 값과 실행 흐름을 확인한다.

### 사용법

```c
/* devices/timer.c */
void timer_sleep(int64_t ticks) {
    int64_t start = timer_ticks();

    printf("[DEBUG] timer_sleep: ticks=%lld, start=%lld\n", ticks, start);

    ASSERT(intr_get_level() == INTR_ON);
    while (timer_elapsed(start) < ticks)
        thread_yield();
}
```

```c
/* threads/thread.c */
void thread_unblock(struct thread *t) {
    printf("[DEBUG] unblock: %s, priority=%d\n", t->name, t->priority);
    // ...
}
```

### 규칙

- `[DEBUG]` 접두사를 붙여서 일반 출력과 구분한다
- 제출 전에 반드시 모든 printf를 제거한다
- `make check` 돌릴 때 printf가 남아있으면 테스트가 FAIL 한다 (출력이 달라지므로)

### 유용한 printf 패턴

```c
/* 현재 스레드 정보 */
printf("[DEBUG] current: %s (pri=%d)\n",
       thread_current()->name, thread_current()->priority);

/* 리스트 크기 확인 */
printf("[DEBUG] ready_list size: %zu\n", list_size(&ready_list));

/* 함수 진입/퇴장 */
printf("[DEBUG] >> lock_acquire: holder=%s\n",
       lock->holder ? lock->holder->name : "none");
printf("[DEBUG] << lock_acquire: got lock\n");

/* 조건 분기 확인 */
if (t->priority > thread_current()->priority) {
    printf("[DEBUG] preempt! %s(%d) > %s(%d)\n",
           t->name, t->priority,
           thread_current()->name, thread_current()->priority);
    thread_yield();
}
```

---

## GDB 디버깅

"왜 안 되지?"를 추적할 때, 또는 커널이 PANIC으로 죽었을 때 사용한다.
매번 쓸 필요는 없다. printf로 해결 안 될 때만 쓴다.

### 준비: 터미널 2개 필요

CLion 하단의 터미널 탭에서 `+` 버튼으로 2개를 연다.

### 터미널 1: Pintos를 GDB 서버 모드로 실행

```bash
source /IdeaProjects/SW_AI-W09-pintos/pintos/activate
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads/build
pintos --gdb -- -q run alarm-multiple
```

"Waiting for gdb connection on localhost:1234..." 메시지가 나오면 대기 상태이다.

### 터미널 2: GDB 클라이언트로 접속

```bash
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads/build
gdb kernel.o
```

GDB 프롬프트가 뜨면 한 줄씩 입력한다. 반드시 한 줄 입력 후 Enter, 응답 확인 후 다음 줄.

```
(gdb) target remote localhost:1234
```
"Remote debugging using localhost:1234" 응답 확인.

```
(gdb) break timer_sleep
```
"Breakpoint 1 at 0x..." 응답 확인.

```
(gdb) continue
```
Pintos가 실행되다가 `timer_sleep`에 진입하면 멈춘다.

### GDB 명령어 요약

```
break <함수명>         breakpoint 설정
break <파일:줄번호>     특정 줄에 breakpoint (예: break timer.c:96)
continue (또는 c)      다음 breakpoint까지 실행
next (또는 n)          다음 줄 실행 (함수 안으로 안 들어감)
step (또는 s)          다음 줄 실행 (함수 안으로 들어감)
print <변수>           변수 값 출력 (예: print ticks)
print *t               구조체 내용 전체 출력
list                    현재 위치 주변 소스코드 표시
backtrace (또는 bt)    호출 스택 표시
info threads            스레드 목록
delete                  모든 breakpoint 삭제
quit                    GDB 종료
```

### 자주 쓰는 디버깅 시나리오

#### 1. 특정 함수에서 멈추고 변수 확인

```
(gdb) break thread_create
(gdb) continue
(gdb) print name
(gdb) print priority
(gdb) next
(gdb) next
```

#### 2. 특정 줄에서 멈추기

```
(gdb) break thread.c:208
(gdb) continue
```

#### 3. 조건부 breakpoint

```
(gdb) break timer_sleep if ticks > 100
(gdb) continue
```
ticks가 100 초과일 때만 멈춘다.

#### 4. 커널 PANIC 시 원인 추적

PANIC이 발생하면 backtrace 주소가 출력된다.

```
Call stack: 0x80042xxxxx 0x80042xxxxx ...
```

별도 터미널에서:

```bash
backtrace kernel.o 0x80042xxxxx 0x80042xxxxx
```

어떤 함수의 몇 번째 줄에서 죽었는지 알 수 있다.

---

## 디버깅 방법 선택 기준

| 상황 | 방법 |
|------|------|
| 변수 값이 궁금하다 | printf |
| 실행 순서가 궁금하다 | printf |
| 테스트가 FAIL인데 이유를 모르겠다 | printf로 기대값과 실제값 비교 |
| 커널이 PANIC으로 죽는다 | GDB로 backtrace 확인 |
| 무한 루프에 빠진다 | GDB로 멈추고 backtrace |
| 특정 조건에서만 버그가 난다 | GDB 조건부 breakpoint |
| 메모리가 깨진다 (magic 값 훼손) | GDB로 스택 확인 |

대부분의 경우 printf로 충분하다. GDB는 printf로 잡기 어려운 문제에만 쓴다.
