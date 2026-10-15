# [W02-01] Project 2: User Programs — 작업 계획서

## 이 문서는 뭔가?

Project 2의 목표는 "유저 프로그램을 실행할 수 있는 OS 만들기"다. 지금 PintOS는 커널만 돌아가고, 유저가 작성한 프로그램(예: `ls`, `cat` 같은)을 실행하지 못한다. 우리가 해야 할 일은 크게 세 가지다:

1. **유저 프로그램에 인자 전달하기** — `./program arg1 arg2`처럼 실행할 때 arg1, arg2를 전달
2. **시스템 콜 구현하기** — 유저 프로그램이 "파일 열어줘", "새 프로세스 만들어줘" 같은 요청을 커널에 보내는 인터페이스
3. **프로세스 관리** — 프로그램 복제(fork), 교체(exec), 종료 대기(wait)

이걸 한 명이 순서대로 하면 오래 걸리니까, 독립적으로 나눌 수 있는 부분을 병렬로 진행한다.

---

## 배경 지식 — 꼭 알아야 할 개념

### 시스템 콜(System Call)이란?

유저 프로그램은 직접 하드웨어(디스크, 화면 등)를 제어할 수 없다. 대신 커널에게 "이거 해줘"라고 부탁하는데, 이 부탁하는 방법이 시스템 콜이다.

예를 들어 C에서 `printf("hello")`를 하면, 내부적으로 `write(1, "hello", 5)` 시스템 콜이 호출된다. 이 `write`는 우리가 구현해야 한다.

### 시스템 콜이 호출되는 과정

```
유저 프로그램: write(1, "hello", 5)
    ↓ (소프트웨어 인터럽트 발생)
커널: syscall_handler() 진입
    ↓ (레지스터에서 콜 번호와 인자를 꺼냄)
    ↓ rax = SYS_WRITE (콜 번호)
    ↓ rdi = 1 (첫 번째 인자: fd)
    ↓ rsi = "hello" 주소 (두 번째 인자: 버퍼)
    ↓ rdx = 5 (세 번째 인자: 크기)
    ↓ (실제 작업 수행)
    ↓ rax에 결과값 저장
유저 프로그램: write() 리턴, 결과 받음
```

### 파일 디스크립터(FD)란?

프로그램이 파일을 열면 OS가 번호를 하나 준다. 이 번호가 파일 디스크립터(File Descriptor)다.

- 0번: 표준 입력 (키보드)
- 1번: 표준 출력 (화면)
- 2번부터: `open()`으로 연 파일들

프로그램은 이 번호만 갖고 "2번 파일에서 읽어줘"처럼 요청한다. 커널 내부에서 번호 → 실제 파일 구조체를 찾아주는 테이블이 FD 테이블이다.

### fork / exec / wait란?

리눅스에서 새 프로그램을 실행하는 패턴이다.

- **fork**: 현재 프로세스를 그대로 복제한다. 부모와 자식 두 프로세스가 된다.
- **exec**: 현재 프로세스의 내용을 새 프로그램으로 교체한다. fork로 복제한 자식에서 exec을 호출하면 자식이 다른 프로그램이 된다.
- **wait**: 부모가 자식이 끝날 때까지 기다린다. 자식의 종료 코드(exit status)를 받아온다.

```
부모 프로세스
    │
    ├─ fork() → 자식 프로세스 생성 (부모의 복사본)
    │               │
    │               ├─ exec("ls") → 자식이 ls 프로그램으로 변신
    │               │
    │               └─ exit(0) → 자식 종료
    │
    └─ wait() → 자식이 끝날 때까지 대기, 종료 코드 0을 받음
```

---

## 채점 비중

| 카테고리 | 비중 | 설명 |
|----------|------|------|
| functionality | 40% | 시스템 콜이 정상적으로 동작하는지 |
| robustness | 30% | 잘못된 입력(NULL 포인터, 잘못된 fd 등)을 줬을 때 커널이 안 죽는지 |
| filesys/base | 15% | 파일 읽기/쓰기가 다양한 크기에서 동작하는지 |
| no-vm | 10% | 메모리가 부족할 때도 안정적으로 동작하는지 |
| dup2 (추가) | +20% | fd 복제 기능 (선택 사항) |

---

## Phase 0 — 공통 기반 (전원 함께 해야 하는 것)

이 단계는 이후 모든 작업의 기초다. 여기서 구조를 잘못 잡으면 나중에 합칠 때 충돌이 심하다. 전원이 같이 설계하고 합의한다.

---

### 0-1. `struct thread` 확장 설계 — 난이도: 중

**이게 뭔가?**

PintOS에서 프로세스 하나는 `struct thread` 하나로 표현된다. 지금은 스레드 이름, 우선순위 정도만 들어있는데, Project 2에서는 "이 프로세스가 어떤 파일을 열었는지", "자식 프로세스가 누구인지" 같은 정보를 추가로 저장해야 한다.

**통과 테스트**: 직접 대응 테스트 없음 (이후 모든 테스트가 이 구조에 의존한다)

**해야 할 일**:

`thread.h`의 `struct thread`에 필드를 추가한다. 아래는 예시이고, 팀 회의에서 이름과 방식을 확정한다.

```c
/* ── 파일 관련 ── */
struct file **fd_table;      /* FD 테이블: 번호 → 파일 포인터 변환용 배열 */
int fd_count;                /* FD 테이블의 크기 (기본 128) */

/* ── 프로세스 부모-자식 관계 ── */
struct list child_list;      /* 내가 만든 자식 프로세스 목록 */
struct list_elem child_elem; /* 부모의 child_list에 연결되는 링크 */
int exit_status;             /* exit(숫자)에서 넘긴 종료 코드 저장 */

/* ── 동기화용 세마포어 ── */
struct semaphore wait_sema;  /* 부모가 wait()으로 기다릴 때 사용 */
struct semaphore fork_sema;  /* fork가 완료될 때까지 부모가 기다릴 때 사용 */

/* ── 상태 플래그 ── */
bool is_waited;              /* 이미 wait된 자식인지 (중복 wait 방지) */

/* ── fork용 ── */
struct intr_frame parent_if; /* fork 시 부모의 CPU 상태 백업 */

/* ── 실행 파일 보호용 ── */
struct file *running_file;   /* 현재 실행 중인 파일 (실행 중 쓰기 금지용) */
```

**세마포어가 뭔가?**

세마포어는 "신호등"이라고 생각하면 된다. 값이 0이면 빨간불(멈춤), 1이상이면 초록불(진행). `sema_down()`은 "초록불 될 때까지 기다림", `sema_up()`은 "초록불로 바꿈"이다. wait/fork에서 부모와 자식의 타이밍을 맞추는 데 쓴다.

**구현 전에 알아야 할 것**:

- PintOS의 `struct list` 사용법: `list_init()`, `list_push_back()`, `list_entry()`, `list_remove()` — `threads/thread.c`에 예시가 많다
- `calloc()`과 `malloc()`의 차이: calloc은 할당 후 0으로 초기화해준다
- 세마포어 기본 동작: `synch.h`의 `sema_init()`, `sema_down()`, `sema_up()` — Project 1에서 이미 사용해봤다
- `struct intr_frame`의 구조: `threads/interrupt.h`에 정의되어 있고, CPU 레지스터 전체를 담는 구조체다

**팀 회의에서 정할 것**:

- FD 테이블 크기: 128칸 고정이면 충분한지?
- 자식 정보를 `struct thread` 자체에 넣을지, 별도 구조체(`struct child_info`)로 뺄지
- stdin(0번)/stdout(1번)을 FD 테이블에 어떻게 표시할지

---

### 0-2. `syscall_handler` 뼈대 — 난이도: 하

**이게 뭔가?**

유저 프로그램이 시스템 콜을 호출하면, 가장 먼저 도착하는 곳이 `syscall_handler()`다. 여기서 "어떤 시스템 콜인지" 판별하고, 맞는 함수로 보내주는 교통정리를 한다.

**통과 테스트**: 직접 대응 없음 (모든 시스템 콜 테스트의 진입점)

**해야 할 일**:

`userprog/syscall.c`에 switch-case 골격을 만든다.

```c
void syscall_handler(struct intr_frame *f) {
    /* f->R.rax: 시스템 콜 번호 (어떤 콜인지)
       f->R.rdi: 첫 번째 인자
       f->R.rsi: 두 번째 인자
       f->R.rdx: 세 번째 인자 */

    switch (f->R.rax) {
        case SYS_HALT:     sys_halt(); break;
        case SYS_EXIT:     sys_exit(f->R.rdi); break;
        case SYS_CREATE:   f->R.rax = sys_create(f->R.rdi, f->R.rsi); break;
        case SYS_REMOVE:   f->R.rax = sys_remove(f->R.rdi); break;
        case SYS_OPEN:     f->R.rax = sys_open(f->R.rdi); break;
        case SYS_CLOSE:    sys_close(f->R.rdi); break;
        case SYS_READ:     f->R.rax = sys_read(f->R.rdi, f->R.rsi, f->R.rdx); break;
        case SYS_WRITE:    f->R.rax = sys_write(f->R.rdi, f->R.rsi, f->R.rdx); break;
        case SYS_FILESIZE: f->R.rax = sys_filesize(f->R.rdi); break;
        case SYS_SEEK:     sys_seek(f->R.rdi, f->R.rsi); break;
        case SYS_TELL:     f->R.rax = sys_tell(f->R.rdi); break;
        case SYS_FORK:     f->R.rax = sys_fork(f->R.rdi, f); break;
        case SYS_EXEC:     f->R.rax = sys_exec(f->R.rdi); break;
        case SYS_WAIT:     f->R.rax = sys_wait(f->R.rdi); break;
        case SYS_DUP2:     f->R.rax = sys_dup2(f->R.rdi, f->R.rsi); break;
        default:           sys_exit(-1); break;
    }
}
```

각 `sys_*` 함수는 빈 껍데기(stub)만 만들어둔다. 실제 내용은 Track A, Track B에서 각각 채운다.

**왜 함수를 분리하나?** — 나중에 Track A와 Track B를 합칠 때, switch-case 안에 코드가 길게 들어있으면 git 충돌이 엄청 나기 때문이다. 함수 본체를 별도로 분리하면 충돌이 switch 블록 몇 줄로 한정된다.

**구현 전에 알아야 할 것**:

- `struct intr_frame`의 레지스터 필드: `f->R.rax`, `f->R.rdi`, `f->R.rsi`, `f->R.rdx`가 뭔지 → `include/threads/interrupt.h` 확인
- x86-64 호출 규약: 인자는 rdi → rsi → rdx → r10 → r8 → r9 순서로 전달된다. PintOS syscall은 최대 3개 인자라서 rdi, rsi, rdx만 쓴다
- `include/lib/syscall-nr.h`에 SYS_HALT, SYS_EXIT 등 syscall 번호가 정의되어 있다
- 리턴값은 `f->R.rax`에 저장한다. 유저 프로그램이 이 값을 syscall의 반환값으로 받는다

---

### 0-3. 유저 포인터 검증 — 난이도: 중

**이게 뭔가?**

유저 프로그램이 시스템 콜을 호출할 때 "이 주소에서 읽어줘"라고 주소를 보내온다. 그런데 이 주소가 엉뚱한 곳(NULL이거나, 커널 영역이거나, 매핑 안 된 곳)이면 커널이 죽을 수 있다. 그래서 주소를 쓰기 전에 "이거 진짜 유효한 유저 주소야?"를 확인하는 함수를 만든다.

**통과 테스트** (robustness, 6점):

이 테스트들은 유저 프로그램이 일부러 잘못된 주소에 접근한다. 커널이 죽지 않고, 해당 프로그램만 `exit(-1)`로 종료시키면 통과다.

| 테스트 | 하는 일 |
|--------|---------|
| `bad-read` | NULL 주소를 읽으려 함 |
| `bad-write` | NULL 주소에 쓰려 함 |
| `bad-jump` | NULL 주소를 함수처럼 실행하려 함 |
| `bad-read2` | 커널 영역 주소(0x8004000000)를 읽으려 함 |
| `bad-write2` | 커널 영역 주소에 쓰려 함 |
| `bad-jump2` | 커널 영역 주소를 함수처럼 실행하려 함 |

**구현 전에 알아야 할 것**:

- 유저 공간과 커널 공간의 경계: PintOS에서 유저 공간은 주소 0 ~ `KERN_BASE`(0x8004000000), 그 위는 커널 공간이다. 유저가 커널 주소를 건드리면 안 된다
- `is_user_vaddr(addr)`: 주소가 유저 공간인지 확인하는 매크로 → `include/threads/vaddr.h`
- `pml4_get_page(pml4, addr)`: 해당 주소에 실제 물리 메모리가 매핑되어 있는지 확인 → `include/threads/mmu.h`. NULL이 리턴되면 매핑 안 된 주소
- 페이지 폴트(Page Fault): CPU가 접근할 수 없는 메모리에 접근하면 발생하는 예외. `userprog/exception.c`의 `page_fault()`에서 처리한다
- 페이지(Page): 메모리를 4KB(4096바이트) 단위로 나눈 블록. "페이지 경계"란 4096의 배수 주소를 말한다. 버퍼가 두 페이지에 걸쳐있으면 양쪽 다 검증해야 한다

**해야 할 일**:

1. 주소 검증 함수 작성 (`userprog/syscall.c`):

```c
/* 주소가 유저 영역이고, 실제로 매핑되어 있는지 확인.
   하나라도 실패하면 해당 프로세스를 exit(-1)로 종료한다. */
static void check_address(const void *addr) {
    if (addr == NULL                            /* NULL인가? */
        || !is_user_vaddr(addr)                 /* 커널 영역인가? */
        || pml4_get_page(thread_current()->pml4, addr) == NULL)  /* 매핑 안 됐나? */
        sys_exit(-1);
}
```

2. 버퍼(연속된 메모리 블록) 검증 함수:

```c
/* read/write에서 사용. 버퍼의 시작과 끝이 모두 유효해야 한다. */
static void check_buffer(const void *buffer, size_t size) {
    check_address(buffer);
    check_address((const char *)buffer + size - 1);
}
```

3. 페이지 폴트 핸들러 수정 (`userprog/exception.c`):

유저 프로그램이 잘못된 주소에 접근하면 CPU가 "페이지 폴트"를 발생시킨다. 현재 PintOS는 이때 커널 패닉(전체 시스템 사망)을 하는데, 유저 모드에서 발생한 폴트는 해당 프로세스만 죽이도록 바꾼다.

```c
/* exception.c의 page_fault() 안에서 */
if (user) {  /* user = 유저 모드에서 발생한 폴트인지 여부 */
    thread_current()->exit_status = -1;
    printf("%s: exit(%d)\n", thread_current()->name, -1);
    thread_exit();  /* 이 프로세스만 종료. 커널은 계속 동작. */
}
```

---

### 0-4. Argument Passing (인자 전달) — 난이도: 중

**이게 뭔가?**

`./myprogram hello world` 이렇게 실행하면, 프로그램의 `main(int argc, char *argv[])`에 `argc=3`, `argv=["myprogram", "hello", "world"]`가 들어와야 한다. 이걸 커널이 유저 스택에 올바른 형식으로 배치해주는 작업이다.

**통과 테스트** (functionality, 5점):

| 테스트 | 하는 일 |
|--------|---------|
| `args-none` | 인자 없이 실행 → argc=1, argv[0]=프로그램이름 |
| `args-single` | 인자 1개 → argc=2 |
| `args-multiple` | 인자 4개 → argc=5 |
| `args-many` | 인자 22개 → argc=23 |
| `args-dbl-space` | `"arg1  arg2"` (공백 2개) → 공백 무시하고 argc=3 |

**구현 전에 알아야 할 것**:

- `strtok_r()` 함수: 문자열을 특정 구분자로 잘라주는 함수. `strtok()`과 달리 재진입 가능(thread-safe)하다 → `include/lib/string.h`
- x86-64 스택 구조: 스택은 높은 주소에서 낮은 주소 방향으로 자란다. `rsp`가 스택의 꼭대기를 가리킨다
- 워드 정렬(word-align): x86-64는 8바이트 단위로 정렬해야 성능이 좋다. `rsp`가 8의 배수가 되도록 패딩을 넣어야 한다
- `hex_dump()` 함수: 메모리 내용을 16진수로 출력하는 디버깅 도구 → `include/lib/stdio.h`. 스택 배치를 확인할 때 필수

**해야 할 일**:

1. `process_exec()`에서 명령어 문자열을 공백으로 잘라 토큰을 만든다.

```c
/* 예: "ls -l /home" → ["ls", "-l", "/home"] */
char *token, *save_ptr;
char *argv[64];
int argc = 0;
for (token = strtok_r(cmd_line, " ", &save_ptr);
     token != NULL;
     token = strtok_r(NULL, " ", &save_ptr))
    argv[argc++] = token;
```

`strtok_r()`은 이중 공백도 자동으로 건너뛰므로 `args-dbl-space`가 저절로 통과된다.

2. 유저 스택에 인자를 배치한다. 스택은 높은 주소에서 낮은 주소로 자란다:

```
높은 주소 ──────────────────────────
  argv[0] 문자열 데이터: "ls\0"
  argv[1] 문자열 데이터: "-l\0"
  argv[2] 문자열 데이터: "/home\0"
  (8바이트 정렬 패딩)
  argv[3] = NULL  ← 끝을 알려주는 표시
  argv[2] 주소  ← "/home" 문자열이 있는 주소
  argv[1] 주소  ← "-l" 문자열이 있는 주소
  argv[0] 주소  ← "ls" 문자열이 있는 주소
  가짜 리턴 주소 (0)
낮은 주소 ──────────────────────────
```

3. 레지스터에 인자 개수와 argv 배열 주소를 넣는다:

```c
f->R.rdi = argc;            /* main의 첫 번째 인자 */
f->R.rsi = argv 배열 시작 주소;  /* main의 두 번째 인자 */
```

4. **디버깅 팁**: `hex_dump()`로 스택 메모리를 출력해보면 배치가 맞는지 눈으로 확인할 수 있다. `args-none`을 가장 먼저 통과시키자.

---

### 0-5. `exit` + `halt` — 난이도: 하

**통과 테스트** (functionality, 2점):

| 테스트 | 하는 일 |
|--------|---------|
| `halt` | `halt()` 호출 → 시스템 전체 종료 |
| `exit` | `exit(57)` 호출 → `"프로그램이름: exit(57)"` 출력 |

**구현 전에 알아야 할 것**:

- `power_off()`: 시스템을 즉시 종료하는 함수 → `include/threads/init.h`
- `thread_exit()`: 현재 스레드를 종료하고 스케줄러에 반환 → `threads/thread.c`. 내부적으로 `process_exit()`을 호출한다
- `printf()`는 커널 모드에서도 사용 가능하다. 종료 메시지 `"프로세스명: exit(코드)"` 형식이 정확해야 테스트를 통과한다

**해야 할 일**:

```c
/* 시스템 전체 전원 끄기 */
static void sys_halt(void) {
    power_off();
}

/* 현재 프로세스 종료 */
static void sys_exit(int status) {
    struct thread *curr = thread_current();
    curr->exit_status = status;
    printf("%s: exit(%d)\n", curr->name, status);
    thread_exit();
}
```

**주의**: `curr->name`에는 프로그램 이름만 들어있어야 한다. `"ls -l /home"` 전체가 들어가면 출력이 틀려진다. `process_exec()`에서 파싱할 때 첫 토큰(프로그램 이름)만 `thread->name`에 복사하자.

---

### Phase 0 완료 시 통과 테스트

| 테스트 | 점수 |
|--------|------|
| args-none, args-single, args-multiple, args-many, args-dbl-space | 5점 |
| halt, exit | 2점 |
| bad-read, bad-write, bad-jump, bad-read2, bad-write2, bad-jump2 | 6점 |
| **합계: 13개** | **13점** |

---

## Phase 1 — 병렬로 나눠서 진행하는 구간

Phase 0을 머지한 뒤, 아래 두 트랙은 수정하는 파일이 거의 겹치지 않아서 동시에 진행할 수 있다.

- **Track A** (파일 시스템 콜): 파일 열기/닫기/읽기/쓰기
- **Track B** (프로세스 시스템 콜): fork/exec/wait

---

### Track A — 파일 시스템 콜

파일 관련 시스템 콜을 구현한다. PintOS가 이미 제공하는 `filesys_create()`, `filesys_open()`, `file_read()` 같은 내부 함수를 감싸서, 유저 프로그램이 번호(fd)로 파일을 다룰 수 있게 만드는 것이 핵심이다.

**주로 수정하는 파일**: `userprog/syscall.c`

---

#### A-1. FD 테이블 구현 — 난이도: 중

**이게 뭔가?**

유저 프로그램이 `open("data.txt")`을 하면, 커널은 파일을 열고 번호(예: 3)를 돌려준다. 이후 `read(3, ...)`, `write(3, ...)`, `close(3)`처럼 번호로 파일을 다룬다. 이 "번호 → 실제 파일"을 연결해주는 배열이 FD 테이블이다.

**통과 테스트**: 직접 대응 없음 (모든 파일 테스트의 전제)

**구현 전에 알아야 할 것**:

- `calloc(n, size)`: n개의 size 크기 메모리를 0으로 초기화해서 할당. `malloc`+`memset(0)`과 같다
- `struct file`: PintOS가 제공하는 파일 구조체. 열린 파일 하나를 나타낸다 → `filesys/file.c`
- 배열 인덱스 = fd 번호. `fd_table[3]`이 NULL이 아니면 3번 fd가 열려있다는 뜻
- 0번(stdin)과 1번(stdout)은 실제 `struct file *`이 아니므로, 특수한 마커값(예: 1, 2)으로 표시해서 일반 파일과 구별한다

**해야 할 일**:

```c
/* 프로세스 생성 시 FD 테이블 초기화 */
curr->fd_table = calloc(128, sizeof(struct file *));
curr->fd_table[0] = (struct file *)1;  /* 0번 = stdin (키보드) 표시 */
curr->fd_table[1] = (struct file *)2;  /* 1번 = stdout (화면) 표시 */
curr->fd_count = 128;
```

FD 할당 함수 — 빈 칸 중 가장 작은 번호를 찾아 파일을 등록한다:

```c
static int process_add_file(struct file *f) {
    struct thread *curr = thread_current();
    for (int fd = 2; fd < curr->fd_count; fd++) {
        if (curr->fd_table[fd] == NULL) {
            curr->fd_table[fd] = f;
            return fd;
        }
    }
    return -1;  /* 빈 칸 없음 */
}
```

FD에서 파일 찾기:

```c
static struct file *process_get_file(int fd) {
    struct thread *curr = thread_current();
    if (fd < 0 || fd >= curr->fd_count)
        return NULL;
    return curr->fd_table[fd];
}
```

프로세스 종료 시 열린 파일 전부 정리:

```c
/* process_exit() 안에서 */
for (int fd = 2; fd < curr->fd_count; fd++) {
    if (curr->fd_table[fd] != NULL)
        file_close(curr->fd_table[fd]);
}
free(curr->fd_table);
```

---

#### A-2. `create` / `remove` — 난이도: 하

**이게 뭔가?**

`create("memo.txt", 100)`은 100바이트짜리 빈 파일을 만드는 것이고, `remove("memo.txt")`는 삭제하는 것이다. 파일을 "만들기만" 하는 거지, 열지(open)는 않는다.

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `create-normal` | 정상 파일 생성 → 성공 | 1 |
| `create-empty` | 빈 문자열 이름 → 실패 | 1 |
| `create-long` | 511자 이름 → 실패 (너무 김) | 1 |
| `create-exists` | 이미 있는 파일 다시 생성 → 실패 | 1 |
| `create-bad-ptr` | 엉뚱한 주소를 파일명으로 → exit(-1) | 1 |
| `create-null` | NULL을 파일명으로 → exit(-1) | 1 |
| `create-bound` | 파일명이 페이지 경계에 걸침 → 정상 동작 | 2 |

**구현 전에 알아야 할 것**:

- `filesys_create(name, initial_size)`: PintOS 제공 함수. 파일 시스템에 파일을 생성한다 → `filesys/filesys.c`
- `filesys_remove(name)`: 파일 삭제. 열려있는 파일도 삭제 가능하지만, 열린 파일의 데이터는 닫힐 때까지 유지된다
- `struct lock`과 `lock_acquire()`/`lock_release()`: 동기화 도구. 한 번에 하나의 스레드만 임계 구역에 진입하게 한다 → `threads/synch.h`. 세마포어(0~1)의 특수한 경우로 생각해도 된다

**해야 할 일**:

```c
static bool sys_create(const char *file, unsigned initial_size) {
    check_address(file);                     /* 주소 검증 */
    if (file[0] == '\0') return false;       /* 빈 문자열 */
    lock_acquire(&filesys_lock);             /* 파일 시스템 락 잡기 */
    bool success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
    return success;
}

static bool sys_remove(const char *file) {
    check_address(file);
    lock_acquire(&filesys_lock);
    bool success = filesys_remove(file);
    lock_release(&filesys_lock);
    return success;
}
```

**filesys_lock이 뭔가?** — PintOS의 파일 시스템 코드는 내부에 동기화가 없다. 두 프로세스가 동시에 파일 시스템 함수를 호출하면 깨질 수 있다. 그래서 전역 락(lock) 하나를 만들어서, 파일 시스템 함수 호출 전에 잡고, 끝나면 풀어준다.

```c
/* syscall.c 상단에 전역으로 선언 */
static struct lock filesys_lock;

/* syscall_init()에서 초기화 */
lock_init(&filesys_lock);
```

---

#### A-3. `open` / `close` — 난이도: 하

**이게 뭔가?**

`open("data.txt")`은 파일을 열고 fd 번호를 받는 것, `close(3)`은 3번 fd를 닫는 것이다.

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `open-normal` | 정상 파일 열기 → fd >= 2 | 1 |
| `open-missing` | 없는 파일 열기 → -1 | 1 |
| `open-twice` | 같은 파일 두 번 열기 → 다른 fd 2개 | 1 |
| `close-normal` | 열고 닫기 → 성공 | 1 |
| `open-bad-ptr` | 엉뚱한 주소 → exit(-1) | 1 |
| `open-null` | NULL → exit(-1) | 1 |
| `open-empty` | 빈 문자열 → -1 | 1 |
| `open-boundary` | 파일명이 페이지 경계에 걸침 | 2 |
| `close-bad-fd` | 엉뚱한 fd → 그냥 무시 | 1 |
| `close-twice` | 이미 닫은 fd → 그냥 무시 | 1 |

**구현 전에 알아야 할 것**:

- `filesys_open(name)`: 파일을 열고 `struct file *`을 반환. 없으면 NULL → `filesys/filesys.c`
- `file_close(file)`: 열린 파일을 닫고 자원 해제 → `filesys/file.c`
- open을 두 번 하면 독립적인 `struct file *`이 두 개 생긴다 (같은 파일이지만 읽기 위치가 각각 독립)

**해야 할 일**:

```c
static int sys_open(const char *file) {
    check_address(file);
    if (file[0] == '\0') return -1;
    lock_acquire(&filesys_lock);
    struct file *f = filesys_open(file);  /* 파일 시스템에서 파일 열기 */
    lock_release(&filesys_lock);
    if (f == NULL) return -1;             /* 파일 없으면 -1 */
    int fd = process_add_file(f);         /* FD 테이블에 등록 */
    if (fd == -1) file_close(f);          /* 테이블 꽉 찼으면 닫기 */
    return fd;
}

static void sys_close(int fd) {
    if (fd < 2) return;                   /* stdin/stdout은 닫을 수 없음 */
    struct file *f = process_get_file(fd);
    if (f == NULL) return;                /* 이미 닫혔거나 잘못된 fd */
    thread_current()->fd_table[fd] = NULL;
    file_close(f);
}
```

---

#### A-4. `read` / `write` — 난이도: 중

**이게 뭔가?**

`read(fd, buffer, size)`: fd에서 size 바이트를 읽어서 buffer에 넣어준다.
`write(fd, buffer, size)`: buffer의 내용 size 바이트를 fd에 쓴다.

특수한 경우:
- fd=0(stdin)에서 read → 키보드 입력 받기
- fd=1(stdout)에 write → 화면에 출력하기

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `read-normal` | 파일 열고 읽기 → 내용 일치 확인 | 1 |
| `read-zero` | 0바이트 읽기 → 0 리턴, 아무 일도 안 함 | 1 |
| `write-normal` | 파일 만들고 쓰기 → 쓴 바이트 수 확인 | 1 |
| `write-zero` | 0바이트 쓰기 → 0 리턴 | 1 |
| `read-bad-fd` | 엉뚱한 fd로 읽기 → -1 | 1 |
| `read-bad-ptr` | 커널 주소 버퍼 → exit(-1) | 1 |
| `read-stdout` | stdout에서 읽기 → -1 | 1 |
| `read-boundary` | 버퍼가 페이지 경계에 걸침 → 정상 동작 | 2 |
| `write-bad-fd` | 엉뚱한 fd로 쓰기 → -1 | 1 |
| `write-bad-ptr` | 잘못된 버퍼 주소 → exit(-1) | 1 |
| `write-stdin` | stdin에 쓰기 → -1 | 1 |
| `write-boundary` | 버퍼가 페이지 경계에 걸침 → 정상 동작 | 2 |

추가로, `filesys/base` 테스트 (15%):
- `sm-*` (5개): 작은 파일 생성/쓰기/읽기
- `lg-*` (5개): 큰 파일로 같은 테스트
- `syn-*` (3개): 여러 프로세스가 동시에 파일 접근 → **Track B가 합쳐져야 통과**

**구현 전에 알아야 할 것**:

- `file_read(file, buffer, size)`: 파일에서 size 바이트를 읽어 buffer에 넣는다. 실제로 읽은 바이트 수를 반환 → `filesys/file.c`
- `file_write(file, buffer, size)`: buffer에서 size 바이트를 파일에 쓴다 → `filesys/file.c`
- `input_getc()`: 키보드에서 한 글자를 읽는 함수 → `include/devices/input.h`
- `putbuf(buffer, size)`: buffer 내용을 콘솔에 출력 → `include/lib/kernel/console.h`
- stdin(fd=0)과 stdout(fd=1)은 파일 시스템이 아니라 각각 키보드/화면이므로, 별도 처리가 필요하다

**해야 할 일**:

```c
static int sys_read(int fd, void *buffer, unsigned size) {
    check_buffer(buffer, size);
    if (size == 0) return 0;

    if (fd == 0) {
        /* 키보드 입력 (stdin) */
        unsigned char *buf = buffer;
        for (unsigned i = 0; i < size; i++)
            buf[i] = input_getc();  /* 한 글자씩 읽기 */
        return size;
    }
    if (fd == 1) return -1;  /* stdout에서는 읽을 수 없음 */

    struct file *f = process_get_file(fd);
    if (f == NULL) return -1;
    lock_acquire(&filesys_lock);
    int bytes = file_read(f, buffer, size);
    lock_release(&filesys_lock);
    return bytes;
}

static int sys_write(int fd, const void *buffer, unsigned size) {
    check_buffer(buffer, size);
    if (size == 0) return 0;

    if (fd == 1) {
        /* 화면 출력 (stdout) */
        putbuf(buffer, size);
        return size;
    }
    if (fd == 0) return -1;  /* stdin에는 쓸 수 없음 */

    struct file *f = process_get_file(fd);
    if (f == NULL) return -1;
    lock_acquire(&filesys_lock);
    int bytes = file_write(f, buffer, size);
    lock_release(&filesys_lock);
    return bytes;
}
```

---

#### A-5. `filesize` / `seek` / `tell` — 난이도: 하

**이게 뭔가?**

- `filesize(fd)`: 파일 크기를 바이트로 반환
- `seek(fd, position)`: 파일 읽기/쓰기 위치를 position으로 이동 (동영상 재생에서 탐색바 옮기는 것과 비슷)
- `tell(fd)`: 현재 읽기/쓰기 위치를 반환

**통과 테스트**: 직접 대응 테스트 없음 (rox, dup2 등에서 간접 사용)

**구현 전에 알아야 할 것**:

- `file_length(file)`: 파일 전체 크기(바이트) 반환 → `filesys/file.c`
- `file_seek(file, position)`: 읽기/쓰기 커서를 position 위치로 이동
- `file_tell(file)`: 현재 커서 위치 반환
- 이 세 함수는 PintOS가 다 구현해뒀다. 우리는 fd → `struct file *` 변환만 해서 호출하면 된다

**해야 할 일**:

```c
static int sys_filesize(int fd) {
    struct file *f = process_get_file(fd);
    if (f == NULL) return -1;
    return file_length(f);  /* PintOS 제공 함수 */
}

static void sys_seek(int fd, unsigned position) {
    struct file *f = process_get_file(fd);
    if (f == NULL) return;
    file_seek(f, position);
}

static unsigned sys_tell(int fd) {
    struct file *f = process_get_file(fd);
    if (f == NULL) return 0;
    return file_tell(f);
}
```

이 함수들도 `filesys_lock`으로 감싸는 게 안전하다.

---

#### Track A 완료 시 통과 테스트

| 테스트 | 점수 |
|--------|------|
| create-normal, create-empty, create-long, create-exists | 4 |
| open-normal, open-missing, open-twice, close-normal | 4 |
| read-normal, read-zero, write-normal, write-zero | 4 |
| robustness (create/open/close/read/write 관련 16개) | 26 |
| filesys/base sm-*, lg-* (10개) | 10 |
| **합계: 약 28개** | **~48점** |

> `syn-*` (3개), `filesys/base` 나머지는 Track B(fork/exec/wait)가 합쳐진 뒤 통과.

---

### Track B — 프로세스 시스템 콜

프로세스의 생성, 교체, 종료 대기를 구현한다. fork가 Project 2에서 가장 어려운 부분이다.

**주로 수정하는 파일**: `userprog/syscall.c`, `userprog/process.c`

---

#### B-1. `exec` — 난이도: 중

**이게 뭔가?**

`exec("ls -l")`을 호출하면, 현재 프로세스가 ls 프로그램으로 바뀐다. 기존 코드, 메모리, 스택이 전부 교체된다. 성공하면 원래 코드로 돌아오지 않는다 (이미 다른 프로그램이 되었으니까).

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `exec-once` | `exec("child-simple")` → 자식 프로그램이 exit(81) | 1 |
| `exec-arg` | `exec("child-args childarg")` → 인자 전달 확인 | 1 |
| `exec-missing` | 없는 프로그램 실행 → -1 | 2 |
| `exec-bad-ptr` | 엉뚱한 주소 → exit(-1) | 1 |
| `exec-boundary` | 명령어 문자열이 페이지 경계에 걸침 | 2 |

**구현 전에 알아야 할 것**:

- `process_exec(cmd_line)`: `userprog/process.c`에 이미 골격이 있다. 내부에서 `load()`를 호출해 ELF 바이너리를 메모리에 올린다
- ELF(Executable and Linkable Format): 리눅스/PintOS 실행 파일 형식. `load()`가 알아서 파싱하므로 우리가 ELF 구조를 직접 다룰 필요는 없다
- `palloc_get_page(PAL_ZERO)`: 커널 메모리에서 4KB 페이지 하나를 할당하고 0으로 초기화 → `threads/palloc.h`. 유저 메모리의 문자열을 커널로 복사할 때 쓴다
- `strlcpy(dst, src, size)`: 안전한 문자열 복사. 버퍼 오버플로 방지 → `include/lib/string.h`
- exec이 성공하면 현재 프로세스가 **새 프로그램으로 완전히 교체**되므로 리턴하지 않는다는 점이 핵심

**해야 할 일**:

```c
static int sys_exec(const char *cmd_line) {
    check_address(cmd_line);

    /* 유저 메모리에 있는 문자열을 커널 메모리로 복사.
       process_exec 안에서 유저 페이지 테이블이 교체되면
       원래 문자열에 접근할 수 없으므로, 미리 복사해둔다. */
    char *cmd_copy = palloc_get_page(PAL_ZERO);
    if (cmd_copy == NULL) sys_exit(-1);
    strlcpy(cmd_copy, cmd_line, PGSIZE);

    int result = process_exec(cmd_copy);
    /* 성공하면 여기 도착하지 않는다 (이미 다른 프로그램이다).
       여기 왔다면 실패한 것이다. */
    return -1;
}
```

`process_exec()` 수정:
- 받은 명령어를 파싱 → 프로그램 이름 추출
- `load()`로 프로그램 파일(ELF 형식)을 메모리에 올림
- 성공하면 argument passing 수행 → 유저 모드로 전환 (`do_iret`)
- 실패하면 `exit(-1)`

---

#### B-2. `wait` — 난이도: 상

**이게 뭔가?**

`wait(child_pid)`를 호출하면, 자식 프로세스가 종료될 때까지 기다렸다가, 자식의 종료 코드를 반환한다.

예: 자식이 `exit(42)`로 끝났으면 부모의 `wait()`이 42를 반환한다.

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `wait-simple` | fork → 자식 exec → 부모 wait → 종료 코드 확인 | 1 |
| `wait-twice` | 같은 자식에 wait 두 번 → 두 번째는 -1 | 1 |
| `wait-bad-pid` | 존재하지 않는 pid로 wait → -1 | 2 |
| `wait-killed` | 자식이 비정상 종료 → 부모가 -1 받음 | 2 |

**구현 전에 알아야 할 것**:

- `tid_t`와 `pid_t`: PintOS에서는 같은 것이다 (스레드 = 프로세스). `tid_t`는 `int` 타입
- 세마포어 기반 부모-자식 동기화: 자식이 아직 살아있으면 부모가 `sema_down`으로 잠들고, 자식이 `exit()`할 때 `sema_up`으로 깨워준다
- `list_entry(elem, struct thread, child_elem)`: PintOS 연결리스트에서 elem으로부터 원래 구조체를 꺼내는 매크로. Project 1에서 사용해봤다
- 자식이 종료된 뒤에도 exit_status를 읽어야 하므로, `struct thread`를 바로 해제하면 안 된다. 부모가 wait으로 읽은 뒤에 해제해야 한다

**해야 할 일**:

```c
static int sys_wait(pid_t pid) {
    return process_wait(pid);
}
```

`process_wait()` 구현:

```c
int process_wait(tid_t child_tid) {
    struct thread *curr = thread_current();

    /* 1. 내 자식 목록에서 child_tid 찾기 */
    struct thread *child = get_child_by_tid(curr, child_tid);
    if (child == NULL) return -1;   /* 내 자식이 아니거나 없는 pid */

    /* 2. 이미 wait한 자식이면 거부 */
    if (child->is_waited) return -1;
    child->is_waited = true;

    /* 3. 자식이 끝날 때까지 기다리기.
       자식이 exit() → sema_up(wait_sema) 하면 여기서 깨어남 */
    sema_down(&child->wait_sema);

    /* 4. 자식의 종료 코드 가져오기 */
    int status = child->exit_status;

    /* 5. 자식 정리 (목록에서 제거) */
    list_remove(&child->child_elem);

    return status;
}
```

**핵심 동기화 흐름**:

```
부모: wait(child_pid) → sema_down(&child->wait_sema) → [잠듦]
                                                           ↑
자식: 작업 수행 → exit(42) → sema_up(&자기->wait_sema) ──┘
                                                   [부모 깨어남, 42 반환]
```

**어려운 점**: 자식이 exit()한 뒤에도 `exit_status`를 읽어야 하므로, 자식의 `struct thread`를 바로 해제하면 안 된다. 부모가 wait()으로 상태를 읽은 뒤에야 해제한다.

---

#### B-3. `fork` — 난이도: 최상

**이게 뭔가?**

`fork()`를 호출하면 현재 프로세스의 복사본이 하나 더 만들어진다. 부모 프로세스에는 자식의 pid가, 자식 프로세스에는 0이 반환된다.

```c
pid_t pid = fork("child");
if (pid == 0) {
    /* 여기는 자식 프로세스 */
    exit(81);
} else {
    /* 여기는 부모 프로세스, pid = 자식의 id */
    int status = wait(pid);  /* 81을 받음 */
}
```

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `fork-once` | 1번 fork → 자식 exit(81) → 부모 wait으로 확인 | 1 |
| `fork-multiple` | 4번 순차 fork → 각 자식이 다른 종료 코드 | 1 |
| `fork-recursive` | 8단계 재귀 fork → 트리 구조 종료 확인 | 2 |
| `multi-recurse` | 15단계 재귀 exec (fork+exec+wait 조합) | 2 |
| `fork-boundary` | fork 인자가 페이지 경계에 걸침 | 2 |

**구현 전에 알아야 할 것**:

- `thread_create(name, priority, func, aux)`: 새 스레드를 만들고 func(aux)를 실행시킨다 → `threads/thread.c`. fork에서는 `__do_fork`를 func으로 넘긴다
- `pml4_create()`: 새 페이지 테이블(가상 → 물리 주소 매핑 테이블)을 만든다 → `threads/mmu.c`
- `pml4_for_each(pml4, func, aux)`: 페이지 테이블의 모든 엔트리를 순회하며 func을 호출한다. 부모의 메모리를 자식에게 복사할 때 쓴다
- `palloc_get_page(PAL_USER)`: 유저 공간용 물리 페이지를 할당. `PAL_ZERO`와 OR하면 0으로 초기화 → `threads/palloc.h`
- `memcpy(dst, src, size)`: 메모리를 바이트 단위로 복사
- `do_iret(intr_frame)`: 인터럽트 프레임의 레지스터 값으로 CPU 상태를 복원하고 유저 모드로 전환 → `threads/interrupt.c`
- fork의 핵심은 "부모의 모든 것(CPU 상태, 메모리, 파일)을 자식에게 복사"하는 것이다. 자식은 fork()의 리턴값으로 0을 받고, 부모는 자식의 pid를 받는다

**해야 할 일**:

1. `sys_fork`:

```c
static pid_t sys_fork(const char *thread_name, struct intr_frame *f) {
    check_address(thread_name);
    return process_fork(thread_name, f);
}
```

2. `process_fork()` — 부모 쪽에서 호출됨:

```c
tid_t process_fork(const char *name, struct intr_frame *if_) {
    struct thread *curr = thread_current();

    /* 부모의 CPU 상태(레지스터)를 저장 → 자식이 나중에 복사한다 */
    memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

    /* 자식 스레드 생성. __do_fork가 자식의 시작 함수가 된다. */
    tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr);
    if (tid == TID_ERROR) return TID_ERROR;

    /* 자식이 복사를 완료할 때까지 기다린다 */
    struct thread *child = get_child_by_tid(curr, tid);
    sema_down(&child->fork_sema);

    /* 자식의 fork가 성공했는지 확인 */
    if (child->exit_status == -1) return TID_ERROR;
    return tid;
}
```

3. `__do_fork()` — 자식 스레드의 시작 함수. 여기서 부모의 모든 것을 복사한다:

```c
static void __do_fork(void *aux) {
    struct thread *parent = (struct thread *)aux;
    struct thread *curr = thread_current();

    /* [1] 부모의 CPU 상태 복사 */
    struct intr_frame if_;
    memcpy(&if_, &parent->parent_if, sizeof(struct intr_frame));
    if_.R.rax = 0;  /* fork()의 리턴값: 자식은 0을 받음 */

    /* [2] 부모의 메모리(페이지 테이블) 복사 */
    curr->pml4 = pml4_create();
    if (curr->pml4 == NULL) goto error;
    if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
        goto error;

    /* [3] 부모의 FD 테이블 복사 (Phase 2에서 구현) */

    /* [4] 복사 완료 알림 → 부모의 sema_down이 풀림 */
    sema_up(&curr->fork_sema);

    /* [5] 자식으로서 유저 모드로 전환 */
    do_iret(&if_);

error:
    curr->exit_status = -1;
    sema_up(&curr->fork_sema);
    thread_exit();
}
```

4. `duplicate_pte()` — 페이지 테이블의 각 엔트리를 하나씩 복사하는 콜백:

```c
static bool duplicate_pte(uint64_t *pte, void *va, void *aux) {
    struct thread *parent = (struct thread *)aux;
    struct thread *curr = thread_current();

    if (is_kernel_vaddr(va)) return true;  /* 커널 영역은 건너뜀 */

    /* 부모의 해당 주소에 있는 물리 페이지를 찾는다 */
    void *parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL) return false;

    /* 자식용 새 물리 페이지를 할당하고 내용을 복사 */
    void *new_page = palloc_get_page(PAL_USER);
    if (new_page == NULL) return false;
    memcpy(new_page, parent_page, PGSIZE);  /* 4KB 통째로 복사 */

    /* 자식의 페이지 테이블에 등록 */
    if (!pml4_set_page(curr->pml4, va, new_page, is_writable(pte))) {
        palloc_free_page(new_page);
        return false;
    }
    return true;
}
```

**fork 전체 흐름 정리**:

```
부모: fork("child") 호출
  → 부모 CPU 상태 저장
  → thread_create() → 자식 스레드 생성
  → sema_down(fork_sema) → 자식이 복사 끝날 때까지 기다림

자식: __do_fork() 시작
  → 부모 CPU 상태 복사 (rax=0으로 변경)
  → 부모 메모리 전체 복사
  → sema_up(fork_sema) → 부모 깨움
  → do_iret() → 유저 모드 진입 (fork() 리턴값 = 0)

부모: sema_down에서 깨어남
  → 자식 pid 반환 (fork() 리턴값 = 자식 pid)
```

---

#### B-4. `exit` 완성 — 난이도: 중

**이게 뭔가?**

Phase 0에서 만든 기본 exit에 프로세스 관계 정리를 추가한다. 종료할 때 부모에게 "나 끝났어"를 알려주고, 자식들에 대한 참조를 정리해야 한다.

**통과 테스트**: 모든 프로세스 테스트가 제대로 끝나려면 이게 필요

**해야 할 일**:

`process_exit()`에서 정리:

```c
void process_exit(void) {
    struct thread *curr = thread_current();

    /* [1] 열린 파일 닫기 (Phase 2 머지 후 활성화) */

    /* [2] 실행 파일 쓰기 잠금 해제 (Phase 2 머지 후 활성화) */

    /* [3] 부모에게 "나 끝났어" 알림 */
    sema_up(&curr->wait_sema);

    /* [4] 페이지 테이블 등 자원 정리 */
    process_cleanup();
}
```

---

#### Track B 완료 시 통과 테스트

| 테스트 | 점수 |
|--------|------|
| fork-once, fork-multiple, fork-recursive | 4 |
| exec-once, exec-arg | 2 |
| wait-simple, wait-twice | 2 |
| multi-recurse | 2 |
| robustness (exec/wait/fork 관련 7개) | 13 |
| **합계: 약 13개** | **~23점** |

---

## Phase 2 — 합친 뒤 마무리

Track A와 Track B를 합친 다음에야 할 수 있는 작업들이다.

---

### 2-1. fork 시 FD 테이블 복제 — 난이도: 중

**이게 뭔가?**

fork로 자식을 만들면, 부모가 열어둔 파일도 자식에게 복사되어야 한다. 단, 자식이 파일을 닫아도 부모의 파일에 영향이 없어야 한다 (독립적인 사본).

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `fork-close` | 자식이 fd 닫아도 부모 fd 살아있음 | 2 |
| `fork-read` | 부모가 20바이트 읽고 fork, 자식이 나머지 읽기 | 2 |
| `exec-read` | fork → exec한 자식도 fd 사용 가능 | 2 |
| `multi-child-fd` | 여러 자식이 각자 fd를 독립적으로 사용 | 2+2 |

**구현 전에 알아야 할 것**:

- `file_reopen(file)`: 같은 파일의 독립적인 사본을 만든다 → `filesys/file.c`. 같은 inode(디스크의 파일)를 가리키지만, 읽기 위치(offset)는 별개다
- fork로 만든 자식과 부모는 같은 파일을 공유하지만, 한쪽이 close해도 다른 쪽에 영향이 없어야 한다. 이걸 `file_reopen()`으로 해결한다
- `inode`: 파일 시스템에서 파일 하나를 나타내는 구조. `struct file`은 inode + 읽기 위치로 구성된다

**해야 할 일**:

`__do_fork()`의 [3] FD 테이블 복사 부분:

```c
/* 부모의 FD 테이블 크기만큼 새로 만들기 */
curr->fd_table = calloc(parent->fd_count, sizeof(struct file *));
curr->fd_count = parent->fd_count;

for (int fd = 0; fd < parent->fd_count; fd++) {
    struct file *f = parent->fd_table[fd];
    if (f == NULL) continue;
    if (fd < 2) {
        curr->fd_table[fd] = f;    /* stdin/stdout 마커 그대로 복사 */
    } else {
        /* file_reopen()은 같은 파일의 독립적인 사본을 만든다.
           자식이 close해도 부모에 영향 없다. */
        curr->fd_table[fd] = file_reopen(f);
    }
}
```

---

### 2-2. 실행 파일 쓰기 금지 (Deny Write to Executables) — 난이도: 중

**이게 뭔가?**

프로그램이 실행 중일 때, 그 프로그램 파일을 수정하면 안 된다 (실행 중인 코드가 바뀌면 위험하니까). 실행 중인 파일에 write를 시도하면 0바이트(거부)를 반환해야 한다.

**통과 테스트**:

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `rox-simple` | 자기 자신의 실행 파일에 write → 0바이트 (거부) | 1 |
| `rox-child` | 자식 실행 중 write 거부, 종료 후 write 허용 | 2 |
| `rox-multichild` | 5개 자식 재귀, 각각 실행 중 write 거부 | 2 |

**구현 전에 알아야 할 것**:

- `file_deny_write(file)`: 해당 파일에 대한 write를 금지한다 → `filesys/file.c`. 이후 `file_write()`를 호출하면 0을 반환(쓰기 거부)
- `file_allow_write(file)`: deny를 해제한다
- 왜 필요한가: 실행 중인 프로그램 파일이 수정되면 프로그램이 오동작할 수 있다. OS가 실행 파일을 보호하는 것은 리눅스에서도 동일하게 동작한다
- `load()` 함수 안에서 파일을 열고 `file_close()`하는 기존 코드가 있을 수 있다. deny_write를 걸려면 파일을 `process_exit()`까지 열어둬야 하므로 close하면 안 된다

**해야 할 일**:

프로그램 로드 시 (`load()` 안에서):

```c
/* 파일을 연 뒤, 쓰기를 금지한다 */
file_deny_write(file);
thread_current()->running_file = file;
/* 주의: load() 끝에서 file_close()하는 기존 코드가 있으면 제거.
   process_exit()까지 파일을 열어둬야 한다. */
```

프로세스 종료 시 (`process_exit()` 안에서):

```c
/* 쓰기 금지를 풀고 파일을 닫는다 */
if (curr->running_file != NULL) {
    file_allow_write(curr->running_file);
    file_close(curr->running_file);
    curr->running_file = NULL;
}
```

---

### 2-3. Robustness 전체 검증 — 난이도: 중

Track A + Track B가 합쳐진 상태에서 전체 robustness 테스트를 돌리고, 실패하는 것을 하나씩 잡는다.

**흔한 실패 원인**:

- `check_address()`에서 페이지 경계의 마지막 바이트를 검증 안 함
- `process_exit()`에서 FD 테이블을 전부 close 안 함 → 메모리 누수
- 자식이 비정상 종료할 때 `sema_up(wait_sema)`를 안 함 → 부모가 영원히 기다림
- fork 실패 시 이미 할당한 페이지를 안 풀어줌

---

### 2-4. `multi-oom` — 난이도: 최상

**이게 뭔가?**

fork를 메모리가 다 찰 때까지 반복한다. 메모리 부족 시 fork가 깨끗하게 -1을 반환하고, 자식들을 전부 wait으로 회수한 뒤, 다시 같은 수만큼 fork가 가능해야 한다. 이걸 10라운드 반복한다.

한 마디로: **메모리 누수가 하나라도 있으면 실패하는 테스트.**

**통과 테스트** (no-vm, 10%):

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `multi-oom` | 10라운드 × 최소 10번 fork | 3 |

**구현 전에 알아야 할 것**:

- `palloc_get_page()` / `palloc_free_page()`: 물리 페이지 할당/해제 함수. 할당한 만큼 반드시 해제해야 메모리 누수가 없다
- `process_cleanup()`: `userprog/process.c`에 있는 페이지 테이블 정리 함수. 유저 페이지를 전부 해제한다
- 메모리 누수: 할당한 메모리를 해제하지 않는 것. 누수가 하나라도 있으면 반복 fork할 때 메모리가 점점 줄어들어 multi-oom이 실패한다
- 이 테스트가 어려운 이유: fork 성공 경로뿐 아니라 실패 경로(메모리 부족)에서도 이미 할당한 자원을 빠짐없이 되돌려야 한다

**확인해야 할 점**:

1. fork 실패 경로: `palloc_get_page()` 실패 시 이미 복사한 페이지를 전부 되돌리는지
2. `process_exit()`에서: 페이지 테이블의 모든 유저 페이지를 `palloc_free_page()`로 반환하는지
3. FD 테이블: 모든 파일을 close하고, 배열 자체를 free하는지
4. `struct thread` 메모리: wait 후 해제되는지

**디버깅 팁**: `palloc.c`에 할당/해제 카운터를 넣어서 라운드 전후를 비교해보면 누수를 찾을 수 있다.

---

### Phase 2 완료 시 추가 통과 테스트

| 테스트 | 점수 |
|--------|------|
| fork-close, fork-read, exec-read, multi-child-fd | 8+2 |
| rox-simple, rox-child, rox-multichild | 5 |
| syn-read, syn-write, syn-remove | 3 |
| multi-oom | 3 |
| **합계: 약 11개** | **~21점** |

---

## Extra — dup2 (+20% 추가 점수)

**이게 뭔가?**

`dup2(oldfd, newfd)`: oldfd가 가리키는 파일을 newfd로도 접근할 수 있게 한다. newfd에 이미 열린 파일이 있으면 먼저 닫는다.

리눅스에서 입출력 리다이렉션(`ls > output.txt`)이 내부적으로 dup2를 사용한다.

**통과 테스트** (dup2, +20%):

| 테스트 | 하는 일 | 점수 |
|--------|---------|------|
| `dup2-simple` | fd 복제 후 두 fd로 같은 파일 읽기 | 1 |
| `dup2-complex` | dup2 체인 + seek/tell + fork + write 종합 | 3 |

**구현 전에 알아야 할 것**:

- POSIX의 dup2 동작: `dup2(oldfd, newfd)`는 oldfd가 가리키는 파일을 newfd 번호로도 접근 가능하게 한다
- `realloc(ptr, new_size)`: 기존 메모리 블록을 새 크기로 재할당. 기존 데이터는 보존된다. FD 테이블을 확장할 때 쓴다
- 참조 카운트 이슈: POSIX에서 dup2는 같은 파일 디스크립션을 공유한다(seek position도 공유). PintOS에서 `file_reopen()`을 쓰면 position이 독립적이 되므로, 테스트가 어떤 동작을 기대하는지 `.ck` 파일에서 확인해야 한다
- 리다이렉션 원리: 셸에서 `ls > output.txt`를 하면, stdout(fd=1)을 output.txt의 fd로 dup2한 뒤 ls를 exec하는 것이다

**해야 할 일**:

```c
static int sys_dup2(int oldfd, int newfd) {
    struct thread *curr = thread_current();

    /* oldfd가 유효한지 확인 */
    if (oldfd < 0 || oldfd >= curr->fd_count) return -1;
    struct file *old_file = curr->fd_table[oldfd];
    if (old_file == NULL) return -1;

    /* oldfd == newfd면 아무것도 안 하고 반환 */
    if (oldfd == newfd) return newfd;

    if (newfd < 0) return -1;

    /* newfd가 테이블 범위를 넘으면 확장 */
    if (newfd >= curr->fd_count) {
        int new_count = newfd + 16;
        curr->fd_table = realloc(curr->fd_table,
                                  new_count * sizeof(struct file *));
        memset(curr->fd_table + curr->fd_count, 0,
               (new_count - curr->fd_count) * sizeof(struct file *));
        curr->fd_count = new_count;
    }

    /* newfd에 이미 파일이 있으면 닫기 */
    if (curr->fd_table[newfd] != NULL && newfd >= 2)
        file_close(curr->fd_table[newfd]);

    /* 복제 */
    if (oldfd < 2)
        curr->fd_table[newfd] = old_file;    /* stdin/stdout 마커 복사 */
    else
        curr->fd_table[newfd] = file_reopen(old_file);  /* 독립 사본 */

    return newfd;
}
```

---

## 전체 테스트 · Phase 맵핑 총괄표

| Phase | 테스트 | 카테고리 | 점수 |
|-------|--------|----------|------|
| **Phase 0** | args-none, args-single, args-multiple, args-many, args-dbl-space | functionality | 5 |
| Phase 0 | halt, exit | functionality | 2 |
| Phase 0 | bad-read, bad-write, bad-jump, bad-read2, bad-write2, bad-jump2 | robustness | 6 |
| **Track A** | create-normal, create-empty, create-long, create-exists | functionality | 4 |
| Track A | open-normal, open-missing, open-twice, close-normal | functionality | 4 |
| Track A | read-normal, read-zero, write-normal, write-zero | functionality | 4 |
| Track A | create-bad-ptr, create-null, create-bound | robustness | 4 |
| Track A | open-bad-ptr, open-null, open-empty, open-boundary | robustness | 5 |
| Track A | close-bad-fd, close-twice | robustness | 2 |
| Track A | read-bad-fd, read-bad-ptr, read-stdout, read-boundary | robustness | 5 |
| Track A | write-bad-fd, write-bad-ptr, write-stdin, write-boundary | robustness | 5 |
| Track A | sm-create~sm-seq-random, lg-create~lg-seq-random | filesys/base | 10 |
| **Track B** | fork-once, fork-multiple, fork-recursive | functionality | 4 |
| Track B | exec-once, exec-arg | functionality | 2 |
| Track B | wait-simple, wait-twice | functionality | 2 |
| Track B | multi-recurse | functionality | 2 |
| Track B | exec-missing, exec-bad-ptr, exec-boundary | robustness | 5 |
| Track B | wait-bad-pid, wait-killed | robustness | 4 |
| Track B | fork-boundary | robustness | 2 |
| **Phase 2** | fork-close, fork-read, exec-read, multi-child-fd | functionality | 10 |
| Phase 2 | rox-simple, rox-child, rox-multichild | functionality | 5 |
| Phase 2 | syn-read, syn-write, syn-remove | filesys/base | 3 |
| Phase 2 | multi-oom | no-vm | 3 |
| **Extra** | dup2-simple, dup2-complex | dup2 | 4 |

---

## 머지 전략

1. **Phase 0 완료 → dev 브랜치에 머지.** 이 커밋을 "공통 기반"으로 삼는다.
2. **Track A → `feat/file-syscalls` 브랜치, Track B → `feat/process-syscalls` 브랜치.** 각각 dev에서 분기한다.
3. **syscall.c 충돌 최소화**: 각 sys_* 함수를 별도 static 함수로 분리해뒀으므로, 머지 시 switch-case 몇 줄만 수동 해결하면 된다.
4. **Track A 먼저 머지 → Track B 머지.** 파일 인프라가 먼저 들어가야 fork의 FD 복제가 수월하다.
5. **Phase 2는 dev 위에서 전원 작업.** deny-write, robustness, multi-oom 등은 둘 다 합쳐져야 테스트 가능하다.

---

## 의존성 흐름 요약

```
Phase 0 (전원 함께)
│  struct thread 설계
│  syscall_handler 뼈대
│  유저 포인터 검증       → bad-* 6개 통과
│  argument passing       → args-* 5개 통과
│  exit + halt            → halt, exit 통과
│
├─────────────────────────┐
▼                         ▼
Track A (파일)            Track B (프로세스)
├ FD 테이블               ├ exec
├ create/remove           ├ wait
├ open/close              ├ fork  ← 가장 어려움
├ read/write              └ exit 완성
└ filesize/seek/tell
│                         │
└────────┬────────────────┘
         ▼
   Phase 2 (합친 뒤)
   ├ fork FD 복제     → fork-close, fork-read 등
   ├ 실행파일 쓰기금지 → rox-* 통과
   ├ robustness 점검
   └ multi-oom         → 메모리 누수 0이어야 통과
         │
         ▼
   Extra: dup2
```
