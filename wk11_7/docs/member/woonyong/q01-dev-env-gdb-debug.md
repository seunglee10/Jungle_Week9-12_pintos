# Q01. Pintos 개발 환경 실행 및 GDB 디버깅 -- Docker, QEMU, GDB 원격 디버깅

> Pintos 개발 환경 | 기본

## 질문

1. CLion + Docker DevContainer에서 Pintos를 어떻게 실행하는가
2. pintos 명령어가 command not found로 나오는 이유는 무엇인가
3. Pintos는 왜 일반 프로그램처럼 Run 버튼으로 실행할 수 없는가
4. GDB 디버깅은 왜 터미널 2개가 필요하고, 어떤 순서로 진행하는가

## 답변

### 최우녕

> CLion + Docker DevContainer에서 Pintos를 어떻게 실행하는가

CLion이 `.devcontainer/Dockerfile` 기반으로 Ubuntu 22.04 컨테이너를 생성한다.
프로젝트가 컨테이너 내부의 `/IdeaProjects/SW_AI-W09-pintos`에 마운트된다.
터미널을 열면 컨테이너 내부 쉘이 뜨고, 여기서 빌드와 실행을 진행한다.

실행 순서:

```bash
# PATH 설정 (최초 1회, 이후 자동 적용)
echo 'source /IdeaProjects/SW_AI-W09-pintos/pintos/activate' >> ~/.bashrc
source ~/.bashrc

# 빌드
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads
make

# 테스트 실행
cd build
pintos -- -q run alarm-multiple
```

> pintos 명령어가 command not found로 나오는 이유는 무엇인가

`pintos` 실행 스크립트는 `pintos/utils/` 디렉터리에 있다.
이 디렉터리가 PATH에 등록되어야 쉘에서 `pintos` 명령어를 인식한다.

`activate` 스크립트가 하는 일은 단 한 가지다:

```bash
# pintos/activate 내용
PATH=$DIR/utils:$PATH
```

`.bashrc`에 등록된 activate 경로가 특정 워크스페이스 절대 경로로 고정되어 있으면
다른 IDE나 다른 로컬 경로에서는 실패한다.
현재 저장소 기준으로는 Dev Container의 `remoteEnv`와 `postCreateCommand`가 워크스페이스 경로를 사용해 `pintos/utils`를 PATH에 추가한다.

> Pintos는 왜 일반 프로그램처럼 Run 버튼으로 실행할 수 없는가

Pintos는 유저 프로그램이 아니라 OS 커널 자체다.
리눅스 위에서 돌아가는 프로그램이 아니라, 하드웨어(QEMU 에뮬레이터) 위에서 직접 부팅된다.

실행 구조:

```
일반 프로그램:  리눅스 커널 -> 프로세스로 실행 -> CLion Run 버튼으로 가능
Pintos:       QEMU (가상 하드웨어) -> Pintos 커널이 직접 부팅 -> pintos 명령어로 실행
```

따라서 `pintos` 명령어가 내부적으로 QEMU를 실행하고, 그 위에서 커널을 부팅시키는 방식이다.

### 수치 검증

`pintos -- -q run alarm-multiple` 실행 결과에서 확인할 수 있는 수치:

```
Timer: 580 ticks                    -- 부팅부터 종료까지 580틱 소요 (580 * 10ms = 5.8초)
Thread: 0 idle ticks, 581 kernel ticks, 0 user ticks
```

idle ticks = 0이 핵심이다. CPU가 한 번도 쉬지 못했다는 뜻이다.
현재는 busy-wait 방식이므로 sleep 중에도 CPU를 계속 소비한다.
Alarm Clock을 구현하면 idle ticks가 크게 증가해야 한다.

TIMER_FREQ = 100이므로:
- 1틱 = 1/100초 = 10ms
- 580틱 = 5.8초
- alarm-multiple에서 thread 4가 50틱씩 7번 = 350틱 = 3.5초가 최대 sleep 시간
- 580 - 350 = 230틱은 다른 스레드 스케줄링 + 부팅 오버헤드

> GDB 디버깅은 왜 터미널 2개가 필요하고, 어떤 순서로 진행하는가

Pintos는 QEMU 안에서 돌아가므로, GDB가 QEMU의 가상 CPU에 원격 접속해야 한다.
이 구조를 "원격 디버깅(remote debugging)"이라 한다.

```
터미널 1: QEMU + Pintos (디버그 대상, GDB 서버 역할)
          pintos --gdb -- -q run alarm-multiple
          "localhost:1234에서 GDB 접속 대기 중..."

터미널 2: GDB (디버거, 클라이언트 역할)
          gdb kernel.o
          (gdb) target remote localhost:1234  -- QEMU에 접속
          (gdb) break timer_sleep             -- 중단점 설정
          (gdb) continue                      -- 실행 시작
```

실제 리눅스 커널 개발자들도 QEMU + GDB 조합으로 디버깅한다.
이것은 Pintos만의 특수한 방식이 아니라 커널 개발의 표준적인 디버깅 방법이다.

GDB 명령어는 반드시 한 줄씩 입력하고 응답을 확인한 뒤 다음 줄을 입력한다.
여러 줄을 한번에 붙여넣으면 명령어가 꼬인다.

대부분의 경우 printf 디버깅이 더 빠르고 편하다.
GDB는 커널 PANIC, 무한 루프, 메모리 훼손 등 printf로 잡기 어려운 문제에만 사용한다.

## 연결 키워드

- 10-dev-workflow.md -- 개발 환경 전체 가이드
- 05-alarm-clock.md -- timer_sleep 구현 (busy-wait 제거)
- 01-big-picture.md -- timer_interrupt 흐름
