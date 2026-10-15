---
name: svg-design-system
description: Python으로 SVG 다이어그램을 생성할 때의 디자인 시스템. SVG, 다이어그램, 플로우차트, 아키텍처 그림, 상태 전이도를 만들 때 활성화. "SVG 만들어", "다이어그램 그려", "구조도", "플로우차트", "상태도"가 언급될 때 이 스킬을 따른다.
---

# SVG 다이어그램 디자인 시스템

> 목적: Python으로 생성하는 모든 SVG 다이어그램에 일관된 디자인을 적용한다.
> 기조: 토스(Toss) 스타일 -- 깔끔한 여백, 부드러운 곡선, 억제된 색상, 높은 가독성.

---

## 색상 팔레트

### 기본 색상

```python
COLORS = {
    "bg":             "#FFFFFF",    # 배경: 순백
    "text_primary":   "#191F28",    # 제목, 핵심 텍스트
    "text_secondary": "#4E5968",    # 설명, 부가 텍스트
    "text_muted":     "#8B95A1",    # 비활성, 힌트
    "border":         "#E5E8EB",    # 기본 테두리
    "shadow":         "#F2F4F6",    # 카드 그림자 (오프셋 rect)
    "surface":        "#F8FAFC",    # 카드 내부 배경 (흰색이 아닌 경우)
    "arrow":          "#CBD5E1",    # 화살표 색상
}
```

### 강조 색상 (의미별)

```python
ACCENT = {
    "blue":   {"bg": "#EFF6FF", "border": "#3B82F6", "text": "#1D4ED8"},
    "green":  {"bg": "#F0FDF4", "border": "#22C55E", "text": "#15803D"},
    "orange": {"bg": "#FFF7ED", "border": "#F97316", "text": "#C2410C"},
    "purple": {"bg": "#FAF5FF", "border": "#A855F7", "text": "#7E22CE"},
    "red":    {"bg": "#FEF2F2", "border": "#EF4444", "text": "#DC2626"},
    "cyan":   {"bg": "#ECFEFF", "border": "#06B6D4", "text": "#0E7490"},
}
```

### 사용 규칙

- 한 다이어그램에서 강조 색상은 최대 4가지까지 사용한다.
- 같은 범주의 요소에는 같은 색상을 일관되게 적용한다.
- 배경이 연한 색이면 테두리는 같은 계열의 진한 색, 텍스트는 더 진한 색을 쓴다.
- 흰색 배경의 카드에는 `border` 색상을 테두리로, `shadow` 색상을 그림자로 쓴다.

---

## 타이포그래피

### 폰트

```python
FONT_UI = "'Inter', 'JetBrains Sans', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
FONT_MONO = "'JetBrains Mono', 'SF Mono', 'Fira Code', 'Cascadia Code', monospace"
```

- `FONT_UI`: 모든 일반 텍스트(제목, 설명, 라벨 등)에 사용한다. SVG 루트 `<svg>` 태그의 `font-family`에 지정한다.
- `FONT_MONO`: 코드/모노스페이스 텍스트에 사용한다. 함수명(`timer_sleep()`, `thread_create()` 등), 타입명(`int`, `struct list_elem` 등), 코드 식별자에 적용한다.

SVG `<style>` 블록에 `.mono` 클래스를 정의하여 모노스페이스 폰트를 적용한다:

```xml
<style>
  .mono { font-family: 'JetBrains Mono', 'SF Mono', 'Fira Code', 'Cascadia Code', monospace; }
</style>
```

### 크기 체계

```python
FONT_SIZE = {
    "title":    32,    # 다이어그램 제목
    "heading":  22,    # 카드 제목, 섹션 헤더
    "body":     18,    # 본문 텍스트
    "label":    15,    # 라벨, 설명
    "caption":  13,    # 배지 내부, 보조 텍스트
}
```

### 규칙

- 제목(title)은 다이어그램당 하나, 상단 중앙에 bold로 배치한다.
- 카드 제목(heading)은 bold, 본문(body)은 regular을 기본으로 한다.
- label과 caption은 `text_secondary` 또는 `text_muted` 색상을 사용한다.
- 함수명, 타입명, 코드 식별자에는 반드시 `FONT_MONO`(`.mono` 클래스)를 적용한다.

---

## 카드

### 구조

모든 정보 블록은 "카드"로 감싼다. 카드는 세 겹으로 구성된다.

```
1. 그림자 rect (3px 오프셋, shadow 색상, 같은 크기)
2. 배경 rect (메인 카드, 테두리 + 채우기)
3. 내부 콘텐츠 (텍스트, 아이콘, 배지)
```

### 속성

```python
CARD = {
    "rx":           12,     # 모서리 둥글기 (일반 카드)
    "rx_large":     20,     # 모서리 둥글기 (큰 섹션 카드)
    "shadow_offset": 3,     # 그림자 오프셋 (px)
    "stroke_width":  2,     # 테두리 두께
    "padding":      20,     # 내부 여백 (최소)
}
```

### 생성 함수

```python
def card(x, y, w, h, fill="#FFFFFF", border="#E5E8EB", rx=12, stroke_w=2):
    """카드를 그린다. 그림자 + 배경 rect."""
    shadow = (
        f'<rect x="{x+3}" y="{y+3}" width="{w}" height="{h}" '
        f'rx="{rx}" fill="#F2F4F6" />'
    )
    main = (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
        f'rx="{rx}" fill="{fill}" stroke="{border}" stroke-width="{stroke_w}" />'
    )
    return shadow + "\n" + main + "\n"
```

---

## 화살표

### 마커 정의

화살표 마커는 작고 우아하게 표현한다. 색상은 `#CBD5E1`(연한 회색-파랑)을 사용한다.

```python
def arrow_marker(marker_id="arrowhead", color="#CBD5E1"):
    return f'''<defs>
  <marker id="{marker_id}" markerWidth="10" markerHeight="7"
          refX="9" refY="3.5" orient="auto">
    <polygon points="0 0, 10 3.5, 0 7" fill="{color}" />
  </marker>
</defs>'''
```

### 곡선 화살표 (기본)

모든 화살표는 직선 대신 **bezier curve**를 사용한다. 수직 화살표는 `C`(cubic) 또는 `Q`(quadratic) 커맨드로 약간의 S-curve를 준다. 수평 화살표도 `Q` 커맨드로 미세한 곡선을 준다.

```python
def arrow_bezier(x1, y1, x2, y2, marker_id="arrowhead", color="#CBD5E1", sw=1.5):
    """두 점 사이를 곡선으로 연결한다. 끝점은 대상 카드에서 8px 전에 멈춘다."""
    # 끝점 8px 단축
    dx, dy = x2 - x1, y2 - y1
    dist = (dx*dx + dy*dy) ** 0.5
    if dist > 0:
        x2 -= 8 * dx / dist
        y2 -= 8 * dy / dist
    # 수직: 약간의 S-curve
    if abs(dy) > abs(dx) * 1.5:
        cx1, cy1 = x1 + 15, y1 + dy * 0.3
        cx2, cy2 = x2 - 15, y1 + dy * 0.7
        d = f"M {x1},{y1} C {cx1},{cy1} {cx2},{cy2} {x2},{y2}"
    else:
        qx = (x1 + x2) / 2
        qy = min(y1, y2) - 10
        d = f"M {x1},{y1} Q {qx},{qy} {x2},{y2}"
    return (
        f'<path d="{d}" fill="none" stroke="{color}" '
        f'stroke-width="{sw}" marker-end="url(#{marker_id})" />'
    )
```

### 자유형 곡선 화살표

복잡한 상태 전이 등에서는 직접 `d` 경로를 지정한다.

```python
def arrow_path(d, marker_id="arrowhead", color="#CBD5E1", sw=1.5):
    return (
        f'<path d="{d}" fill="none" stroke="{color}" '
        f'stroke-width="{sw}" marker-end="url(#{marker_id})" />'
    )
```

### 규칙

- 화살표 색상: `#CBD5E1` (연한 회색-파랑). 기존 `#8B95A1`은 사용하지 않는다.
- stroke-width: 1.5px (기본).
- 화살표 끝점은 카드 테두리에서 **최소 8px** 떨어뜨린다.
- 화살표 마커 크기: markerWidth="10", markerHeight="7" (작고 우아하게).
- **짧은 수직 연결 (100px 이하)**: 직선(`<line>`)을 사용한다. 같은 열에서 바로 아래 카드로 내려가는 연결이 해당한다.
- **먼 거리, 대각선, 되돌아가는 연결 (100px 초과)**: bezier curve(`<path>`)를 사용한다.
- 화살표 라벨은 화살표 위에 겹치지 않고 **옆이나 위쪽에** 배치한다 (y = arrow_y - 12 이상).
- 강조 화살표(Phase 연결 등)는 해당 Phase의 border 색상을 사용할 수 있다.

---

## 배지

작은 라벨을 표시할 때 사용한다 (Phase 번호, 타이밍 정보 등).

### 둥근 배지

```python
def badge(x, y, text_content, bg_color, text_color="#FFFFFF", w=80, h=26):
    rx = h // 2
    return (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
        f'rx="{rx}" fill="{bg_color}" />\n'
        f'<text x="{x + w//2}" y="{y + h//2 + 5}" '
        f'font-size="13" fill="{text_color}" text-anchor="middle" '
        f'font-weight="bold">{text_content}</text>'
    )
```

### 규칙

- 배지 높이: 26px (캡션 크기 + 여백)
- 배지의 rx: 높이의 절반 (완전한 둥근 모서리)
- 진한 배경에 흰 텍스트, 또는 연한 배경에 진한 텍스트
- 배지 오른쪽 가장자리와 텍스트 시작 사이 간격: **최소 12px**

---

## 텍스트 유틸리티

```python
def text(x, y, content, size=18, fill="#191F28", anchor="start", weight="normal", mono=False):
    cls = ' class="mono"' if mono else ""
    return (
        f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" '
        f'text-anchor="{anchor}" font-weight="{weight}"{cls}>{content}</text>'
    )
```

---

## SVG 루트 템플릿

### 필수 속성

```python
def svg_header(w, h, title_text):
    FONT_UI = "'Inter', 'JetBrains Sans', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
    FONT_MONO = "'JetBrains Mono', 'SF Mono', 'Fira Code', 'Cascadia Code', monospace"
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="{w}" height="{h}" viewBox="0 0 {w} {h}"
     font-family="{FONT_UI}">
  <style>
    .mono {{ font-family: {FONT_MONO}; }}
  </style>
  <rect width="{w}" height="{h}" fill="#FFFFFF"/>
  <text x="{w//2}" y="52" text-anchor="middle"
        font-size="32" font-weight="bold" fill="#191F28">
    {title_text}
  </text>
'''
```

### 중요: width와 height를 반드시 명시한다

`viewBox`만 지정하면 GitHub에서 SVG가 매우 작게 렌더링된다.
`width`와 `height`를 명시적으로 설정해야 의도한 크기로 표시된다.

### 권장 크기

| 용도 | width | height |
|------|-------|--------|
| 일반 다이어그램 | 1200 | 800~1000 |
| 넓은 타임라인 | 1400 | 800 |
| 세로로 긴 목록/테이블 | 1200 | 1000~1200 |

---

## 레이아웃 및 간격 규칙

### 여백

```
다이어그램 외곽 여백: 최소 60px
카드 간 간격: 최소 20px
카드 내부 여백: 최소 20px
```

### 텍스트 간격

```
멀티라인 텍스트 줄 높이: 28px (이전 24px에서 변경)
배지와 텍스트 사이 간격: 최소 12px (배지 오른쪽 가장자리 기준)
카드 헤더에서 첫 번째 콘텐츠까지: 최소 20px
컬럼 헤더에서 첫 번째 데이터 행까지: 최소 16px
테이블 행 높이: 36px (필드 목록에서)
```

### 하단 여유

```
SVG viewBox 높이에 20px 추가 여유 확보
마지막 요소에서 하단 가장자리까지 최소 40px 여유
```

### 정렬

- 카드는 그리드에 맞춰 정렬한다 (x축 또는 y축 기준선 통일).
- 제목은 다이어그램 상단 중앙, y=52 위치에 고정한다.
- 범례가 있으면 하단에 배치한다.

### 방향

- 흐름도: 위에서 아래로 (수직)
- 상태도: 왼쪽에서 오른쪽으로 (수평) 또는 자유 배치
- 타임라인: 왼쪽에서 오른쪽으로 (수평)
- 구조도: 위에서 아래로 (수직)

---

## 금지 사항

- 이모지 사용 금지
- 그라데이션 금지 (플랫 디자인 유지)
- 3D 효과 금지 (그림자는 오프셋 rect만 허용)
- 과도한 장식 금지 (아이콘, 클립아트 등)
- 5가지 이상 색상 동시 사용 금지
- viewBox 없이 width/height만 사용하는 것 금지 (둘 다 명시)
- 폰트 크기 12px 미만 금지
- 직선 화살표(`<line>`) 금지 -- bezier curve(`<path>`)를 사용

---

## 다이어그램 유형별 가이드

### 아키텍처 구조도

- 모듈/파일을 카드로 표현한다.
- 카드 내부에 주요 함수를 나열한다 (모노스페이스 폰트).
- 호출 관계를 곡선 화살표로 연결한다.
- 화살표 라벨은 화살표 위에 배치한다 (겹치지 않게).
- 카드 색상으로 범주를 구분한다.

### 상태 전이도

- 각 상태를 큰 둥근 카드(rx=20)로 표현한다.
- 전이를 bezier curve 화살표로 연결한다.
- 복귀 화살표(RUNNING -> READY 등)는 넓은 호를 그린다.
- 화살표 옆에 트리거 함수명을 모노스페이스로 표기한다.
- Phase 배지로 어떤 단계에서 변경되는지 표시한다.

### 플로우차트

- 시작점을 상단 중앙에 둔다.
- 분기를 들여쓰기된 카드로 표현한다.
- 조건부 분기는 배지로 조건을 표시한다.
- 수직 bezier curve 화살표로 연결한다.

### 타임라인

- 수평 축을 중앙에 그린다.
- 시점마다 원형 마커(r=6)를 찍고, 흰색 stroke ring을 추가한다.
- 상단에 설명 카드, 하단에 배지를 배치한다.
- 비교 섹션이 있으면 타임라인 아래에 대비 카드를 둔다.

### 데이터 구조/필드 목록

- 그룹별로 큰 카드로 감싼다.
- 내부에 행 단위로 필드를 나열한다 (행 높이 36px).
- 행 구분은 얇은 선(opacity 0.4)으로 한다.
- 컬럼: Type (모노스페이스), Field, Description 순서.
- 컬럼 헤더와 첫 데이터 행 사이에 16px 간격을 둔다.

---

## 체크리스트

SVG를 생성한 뒤 아래를 확인한다.

```
[ ] width와 height 속성이 <svg> 태그에 있는가
[ ] viewBox가 width/height와 일치하는가
[ ] 폰트 크기가 13px 이상인가
[ ] 배경이 #FFFFFF로 설정되었는가
[ ] 카드에 그림자가 있는가
[ ] 색상이 팔레트 내에서만 사용되었는가
[ ] 텍스트가 카드 밖으로 넘치지 않는가
[ ] 이모지가 없는가
[ ] GitHub에서 의도한 크기로 렌더링되는가
[ ] 함수명/코드에 FONT_MONO가 적용되었는가
[ ] 화살표가 bezier curve인가 (직선 아님)
[ ] 화살표 끝점이 카드에서 8px 이상 떨어져 있는가
[ ] 화살표 라벨이 화살표와 겹치지 않는가
[ ] 마지막 요소에서 하단까지 40px 이상 여유가 있는가
```

---

## 빠른 참조

| 요소 | 속성 | 값 |
|------|------|-----|
| 배경 | fill | #FFFFFF |
| 제목 | font-size, fill | 32px, #191F28 |
| 카드 제목 | font-size, fill | 22px, #191F28 |
| 본문 | font-size, fill | 18px, #191F28 |
| 라벨 | font-size, fill | 15px, #4E5968 |
| 캡션/배지 | font-size | 13px |
| UI 폰트 | font-family | Inter, JetBrains Sans + fallbacks |
| 코드 폰트 | font-family | JetBrains Mono + fallbacks |
| 카드 모서리 | rx | 12px (일반), 20px (대형) |
| 카드 테두리 | stroke-width | 2px |
| 카드 그림자 | offset | 3px |
| 화살표 마커 | size | 10x7 |
| 화살표 선 | stroke-width | 1.5px |
| 화살표 색상 | stroke | #CBD5E1 |
| 화살표 유형 | path | bezier curve (Q/C) |
| 화살표 끝점 간격 | gap | 8px |
| 멀티라인 줄 높이 | line-height | 28px |
| 배지-텍스트 간격 | gap | 12px 이상 |
| 카드 헤더-콘텐츠 간격 | gap | 20px 이상 |
| 컬럼 헤더-행 간격 | gap | 16px |
| 테이블 행 높이 | height | 36px |
| 하단 여유 | clearance | 40px 이상 |
| 다이어그램 크기 | width x height | 1200x800 이상 |
| 외곽 여백 | padding | 60px 이상 |
