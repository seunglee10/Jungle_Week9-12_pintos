#ifndef FILESYS_FAT_H
#define FILESYS_FAT_H

#include "devices/disk.h"
#include "filesys/file.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t cluster_t;  /* FAT 내부에서의 클러스터 인덱스. */

#define FAT_MAGIC 0xEB3C9000 /* FAT 디스크를 식별하는 MAGIC 값 */
#define EOChain 0x0FFFFFFF   /* 클러스터 체인의 끝 */

/* FAT 정보가 들어 있는 섹터들. */
#define SECTORS_PER_CLUSTER 1 /* 클러스터당 섹터 수 */
#define FAT_BOOT_SECTOR 0     /* FAT 부트 섹터 */
#define ROOT_DIR_CLUSTER 1    /* 루트 디렉터리용 클러스터 */

void fat_init (void);
void fat_open (void);
void fat_close (void);
void fat_create (void);
void fat_close (void);

cluster_t fat_create_chain (
    cluster_t clst /* 늘릴 클러스터 번호, 0이면 새 체인 생성 */
);
void fat_remove_chain (
    cluster_t clst, /* 제거할 클러스터 번호 */
    cluster_t pclst /* clst의 이전 클러스터, 0이면 clst가 체인의 시작 */
);
cluster_t fat_get (cluster_t clst);
void fat_put (cluster_t clst, cluster_t val);
disk_sector_t cluster_to_sector (cluster_t clst);

#endif /* filesys/fat.h */
