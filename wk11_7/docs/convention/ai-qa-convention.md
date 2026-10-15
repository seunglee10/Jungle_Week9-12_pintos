---
name: ai-qa-convention
description: AI를 학습 도구로 활용할 때의 원칙과, AI에게 질문한 내용을 문서로 정리하는 컨벤션. "AI 질문", "Claude 질문", "ChatGPT 질문", "AI 사용", "질문 정리"가 언급될 때 활성화.
---

# AI 활용 및 Q&A 문서화 컨벤션

> 목적: AI를 학습에 최대한 활용하되, 코드를 대신 짜게 하지 않는다.
> AI를 사용한 경우 반드시 질문과 답을 문서로 남긴다.

---

## AI 사용 원칙

### 허용

- 모르는 개념, 용어, 동작 원리에 대한 질문
- C 문법, 매크로, 포인터 연산 등 언어 규칙 질문
- 에러 메시지 해석 요청
- 설계 접근 방식에 대한 아이디어 토론
- 공식 문서/매뉴얼 내용 요약 요청
- 디버깅 방향 상담 (증상을 설명하고 원인 추론 요청)

### 금지

- 구현 코드 직접 요청 ("이 함수 짜줘", "이거 구현해줘")
- 테스트 통과용 코드 요청
- 다른 사람의 풀이를 붙여넣고 "설명해줘" (자기 코드만 허용)
- AI가 생성한 코드를 그대로 복붙하여 제출

### 경계선

아래는 상황에 따라 판단한다.

- "이 의사코드를 C로 바꾸면 어떻게 되나" -- 본인이 의사코드를 작성한 경우 허용
- "이 에러가 나는데 원인이 뭘까" + 코드 붙여넣기 -- 본인 코드이면 허용
- "list_insert_ordered 사용법 예시 보여줘" -- API 사용법은 허용 (구현 로직은 금지)

---

## Q&A 문서 형식

AI에게 질문한 내용은 반드시 아래 형식으로 정리하여 개인 브랜치에 기록한다.

### 파일 위치

```
docs/member/<id>/
    q01-<주제-slug>.md
    q02-<주제-slug>.md
    ...
    README.md
```

팀원별로 `docs/member/<id>/` 디렉터리를 만들고, 각자 번호를 붙여 문서를 관리한다.
id는 GitHub 아이디 또는 팀 내에서 쓰는 식별자를 사용한다.

### 파일 네이밍

- 번호는 순서대로 q01, q02, q03, ...
- slug는 kebab-case로 핵심 키워드 2~4개
- 예시: `q01-list-entry-offsetof.md`, `q03-priority-donation-nested.md`

### 문서 구조

```markdown
# Q번호. 제목 -- 핵심 키워드 나열

> 출처 | 난이도

## 질문

번호를 매겨서 질문을 나열한다.
한 문서에 관련 질문 1~5개를 묶는다.

1. 첫 번째 질문
2. 두 번째 질문
3. 세 번째 질문

## 답변

### 본인 이름

각 질문에 대한 답변을 서술한다.
AI가 답한 내용을 그대로 복붙하지 않고, 자기 말로 재구성한다.
코드 블록, 다이어그램, 표를 적극 활용한다.

> 인용 블록으로 원래 질문을 반복하고 그 아래에 답을 쓰면 읽기 쉽다.

### 수치 검증

공식이나 계산이 포함된 주제는 반드시 실제 숫자를 넣어 계산 과정을 보여준다.
"이해했다"가 아니라 "직접 계산해서 맞는지 확인했다"가 기준이다.

## 연결 키워드

- 관련 문서 링크
- 다음에 이어서 볼 내용
```

### 예시

```markdown
# Q01. list_entry 매크로의 동작 원리 -- offsetof, 포인터 역산

> Pintos lib/kernel/list.h | 기본

## 질문

1. list_entry 매크로는 list_elem 포인터로부터 어떻게 부모 구조체의 주소를 알아내는가
2. offsetof 매크로는 왜 (TYPE *)0 을 사용하는가
3. 하나의 구조체에 list_elem이 여러 개 있으면 어떻게 구분하는가

## 답변

### 최우녕

> list_entry 매크로는 list_elem 포인터로부터 어떻게 부모 구조체의 주소를 알아내는가

list_elem의 주소에서 해당 멤버의 오프셋을 빼면 구조체 시작 주소가 나온다.

### 수치 검증

struct thread의 메모리 레이아웃을 바이트 단위로 계산한다.

| 필드 | 크기 | 오프셋 |
|------|------|--------|
| tid (int) | 4B | 0 |
| status (enum) | 4B | 4 |
| name (char[16]) | 16B | 8 |
| priority (int) | 4B | 24 |
| elem.prev (ptr) | 8B | 28 |
| elem.next (ptr) | 8B | 36 |

offsetof(struct thread, elem) = 28

elem의 주소가 0x101C라면:
  0x101C - 28(0x1C) = 0x1000 = struct thread 시작 주소

검산: 0x1000 + 28 = 0x101C (elem 위치와 일치)

## 연결 키워드

- 03-data-structures.md -- list_entry 상세 설명
- 04-synchronization.md -- semaphore waiters에서 list_entry 사용
```

### 수치 검증이 필요한 대표 주제

- **MLFQS 공식**: 실제 nice, recent_cpu, load_avg 값을 넣고 priority를 계산
- **fixed-point 연산**: 정수 -> 고정소수점 변환, 곱셈/나눗셈 중간값 확인
- **timer tick 타이밍**: TIMER_FREQ=100일 때 1초, 4틱, 100틱이 실제 몇 ms인지
- **list_entry/offsetof**: 구조체 필드별 오프셋을 바이트 단위로 직접 계산

---

## README 작성

각 팀원의 Q&A 폴더에 README.md를 두고, 질문 목록을 인덱스로 관리한다.

```markdown
# 이름 -- Pintos Project 1 질문 정리

## 인덱스

| 번호 | 주제 | 파일 |
|------|------|------|
| q01 | list_entry와 offsetof 동작 원리 | [q01-list-entry-offsetof.md](./q01-list-entry-offsetof.md) |
| q02 | thread_block과 thread_yield 차이 | [q02-thread-block-yield.md](./q02-thread-block-yield.md) |
| q03 | priority donation 중첩 시나리오 | [q03-priority-donation-nested.md](./q03-priority-donation-nested.md) |
```

---

## 핵심 원칙 요약

- AI에게 코드를 요구하지 않는다
- 개념, 문법, 동작 원리 질문은 적극 활용한다
- AI를 사용했으면 반드시 Q&A 문서를 남긴다
- 문서는 AI 답변을 그대로 복붙하지 않고 자기 말로 재구성한다
- 팀원 누구나 읽을 수 있도록 정리한다
