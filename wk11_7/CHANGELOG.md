# Changelog

이 문서는 이 저장소의 주요 변경 사항을 기록합니다.

형식 기준:

- 버전 표기: `[x.y.z] - YYYY-MM-DD`
- 카테고리: `Added`, `Changed`, `Fixed`, `Removed`, `Security`, `Docs`, `Refactored`
- 단순 커밋 나열보다 "실제로 무엇이 바뀌었는가" 기준으로 정리합니다.

[0.4.0] - 2026-05-05
Docs
- 루트 `README.md`를 현재 Pintos 저장소 기준 안내 문서로 전면 재작성
- `CHANGELOG.md` 추가 및 README에서 바로 접근할 수 있도록 연결
- 취소된 PR의 비중복 차이를 `docs/merge-recovery/`에 복구 아카이브로 정리

Changed
- `dev`와 `woonyong` 브랜치를 같은 기준 커밋으로 다시 동기화

Fixed
- `fork` 시스템 콜 진입 경로가 머지 과정에서 빠졌던 문제를 복구

[0.3.0] - 2026-05-05
Added
- `child_status` 구조 추가
- `fork_args` 구조 추가
- 부모가 자식 상태를 추적할 수 있도록 `child_status_list`, `self_status` 필드 추가

Changed
- `process_fork()`에서 자식 상태 레코드 생성, 등록, 대기 흐름 추가
- `__do_fork()`에서 부모 실행 문맥 복사와 주소 공간 복제 골격 정리

Fixed
- `thread` 구조체 선언 문법 오류 수정

[0.2.0] - 2026-05-05
Added
- `create`, `open`, `close` 시스템 콜 흐름 추가
- fd 테이블 helper 함수 추가

Changed
- `process_exit()`에서 열린 파일 디스크립터 정리 흐름 추가
- `write()`가 stdout 외 파일 디스크립터 쓰기 경로를 처리하도록 확장

[0.1.0] - 2026-05-04
Added
- Pintos Project 2 프로세스 테스트 문서 추가
- 구현 가이드, 팀 전략, 컨벤션 문서 정리

Docs
- 주석 구조와 프로세스 관련 문서 보강
