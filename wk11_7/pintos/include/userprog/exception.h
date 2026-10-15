#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/*
 * 예외의 원인을 설명하는 페이지 폴트 오류 코드 비트들.
 */
/*
 * 0이면 페이지가 존재하지 않고, 1이면 접근 권한 위반이다.
 */
#define PF_P 0x1
/*
 * 0이면 읽기이고, 1이면 쓰기이다.
 */
#define PF_W 0x2
/*
 * 0이면 커널, 1이면 유저 프로세스이다.
 */
#define PF_U 0x4

void exception_init (void);
void exception_print_stats (void);

#endif /* userprog/exception.h */
