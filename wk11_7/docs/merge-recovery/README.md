# Merge Recovery

취소된 PR `#146`, `#147`, `#148`에서 현재 `woonyong` 브랜치에 아직 반영되지 않은 차이를
패치 파일로 복구해 둔 디렉터리다.

기준은 다음과 같다.

- 비교 기준: 현재 `woonyong` HEAD
- 복구 형태: `git diff HEAD..prXXX`
- 제외 항목: `.vscode/` 관련 설정 파일

파일 설명:

- `pr146-remaining.diff`: PR `#146`의 남은 차이
- `pr147-remaining.diff`: PR `#147`의 남은 차이
- `pr148-remaining.diff`: PR `#148`의 남은 차이

이 패치들은 "취소된 PR에 있었지만 현재 브랜치에는 없는 코드 조각"을
그대로 다시 확인할 수 있게 남겨 둔 아카이브다.
