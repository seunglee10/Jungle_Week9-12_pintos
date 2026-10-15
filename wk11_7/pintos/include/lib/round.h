#ifndef __LIB_ROUND_H
#define __LIB_ROUND_H

/* X를 STEP의 배수 중 가장 가까운 위쪽 값으로 올림한다.
 * X >= 0, STEP >= 1인 경우만 사용한다. */
#define ROUND_UP(X, STEP) (((X) + (STEP) - 1) / (STEP) * (STEP))

/* X / STEP 값을 올림해 반환한다.
 * X >= 0, STEP >= 1인 경우만 사용한다. */
#define DIV_ROUND_UP(X, STEP) (((X) + (STEP) - 1) / (STEP))

/* X를 STEP의 배수 중 가장 가까운 아래쪽 값으로 내림한다.
 * X >= 0, STEP >= 1인 경우만 사용한다. */
#define ROUND_DOWN(X, STEP) ((X) / (STEP) * (STEP))

/* DIV_ROUND_DOWN은 따로 없다. 그냥 X / STEP이면 된다. */

#endif /* lib/round.h */
