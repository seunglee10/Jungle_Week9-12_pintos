#ifndef FILESYS_OFF_T_H
#define FILESYS_OFF_T_H

#include <stdint.h>

/* 파일 내부의 오프셋.
 * 여러 헤더가 이 정의만 필요로 하고 다른 정의들은 필요로 하지 않기 때문에
 * 별도의 헤더로 분리했다.
 */
typedef int32_t off_t;

/* printf()용 서식 지정자. 예:
 * printf ("offset=%"PROTd"\n", offset);
 */
#define PROTd PRId32

#endif /* filesys/off_t.h */
