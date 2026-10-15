#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* 시스템 파일 inode의 섹터들. */
#define FREE_MAP_SECTOR 0       /* free map 파일의 inode 섹터. */
#define ROOT_DIR_SECTOR 1       /* 루트 디렉터리 파일 inode 섹터. */

/* 파일 시스템에 사용되는 디스크. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

#endif /* filesys/filesys.h */
