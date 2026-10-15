# Convention 문서 안내

`docs/convention`은 팀과 AI 도구가 같은 기준으로 작업하기 위한 컨벤션 문서 모음입니다.

## 문서 목록

| 파일 | 설명 | 사용할 때 |
|------|------|-----------|
| `ai-qa-convention.md` | AI 사용 원칙과 Q&A 작성 기준을 정리한다. | AI에게 질문하거나 답변 기록을 남길 때 |
| `c-coding-standard.md` | C/Pintos 코드 작성 스타일을 정리한다. | C 코드 포맷, 네이밍, 함수 작성 기준이 필요할 때 |
| `code-structure-convention.md` | 함수 배치, 보조 함수 위치, 변수 범위, 전역 변수 사용 기준을 정리한다. | 새 함수나 변수를 어디에 둘지 판단할 때 |
| `comment-convention.md` | 영어 원문 주석 번역, `@lock` 태그, 추가 주석 작성 규칙을 정리한다. | Pintos 원본 주석을 번역하거나 새 주석을 추가할 때 |
| `commit-convention.md` | 커밋 메시지, 브랜치 전략, PR 작성 기준을 정리한다. | 커밋 메시지나 PR 설명을 작성할 때 |

## 권장 사용 순서

1. C/Pintos 코드를 작성할 때는 `c-coding-standard.md`와 `code-structure-convention.md`를 함께 적용한다.
2. 주석을 번역하거나 추가할 때는 `comment-convention.md`를 우선 적용한다.
3. 작업이 끝난 뒤 커밋 메시지는 `commit-convention.md` 기준으로 작성한다.
4. AI를 활용한 경우 `ai-qa-convention.md` 기준으로 Q&A를 기록한다.
