#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* free map 파일. */
static struct bitmap *free_map;      /* free map, 디스크 섹터마다 1비트. */

/* free map을 초기화한다. */
void
free_map_init (void) {
	free_map = bitmap_create (disk_size (filesys_disk));
	if (free_map == NULL)
		PANIC ("bitmap creation failed--disk is too large");
	bitmap_mark (free_map, FREE_MAP_SECTOR);
	bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* free map에서 연속된 CNT개 섹터를 할당하고
 * 첫 번째 섹터를 *SECTORP에 저장한다.
 * 성공하면 true를, 사용할 수 있는 섹터가 없으면 false를 반환한다.
 */
bool
free_map_allocate (size_t cnt, disk_sector_t *sectorp) {
	disk_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
	if (sector != BITMAP_ERROR
			&& free_map_file != NULL
			&& !bitmap_write (free_map, free_map_file)) {
		bitmap_set_multiple (free_map, sector, cnt, false);
		sector = BITMAP_ERROR;
	}
	if (sector != BITMAP_ERROR)
		*sectorp = sector;
	return sector != BITMAP_ERROR;
}

/* SECTOR부터 시작하는 CNT개 섹터를 다시 사용할 수 있게 만든다. */
void
free_map_release (disk_sector_t sector, size_t cnt) {
	ASSERT (bitmap_all (free_map, sector, cnt));
	bitmap_set_multiple (free_map, sector, cnt, false);
	bitmap_write (free_map, free_map_file);
}

/* free map 파일을 열고 디스크에서 읽어 온다. */
void
free_map_open (void) {
	free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
	if (free_map_file == NULL)
		PANIC ("can't open free map");
	if (!bitmap_read (free_map, free_map_file))
		PANIC ("can't read free map");
}

/* free map을 디스크에 기록하고 free map 파일을 닫는다. */
void
free_map_close (void) {
	file_close (free_map_file);
}

/* 디스크에 새 free map 파일을 만들고 free map을 그 안에 기록한다. */
void
free_map_create (void) {
	/* inode를 만든다. */
	if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map)))
		PANIC ("free map creation failed");

	/* bitmap을 파일에 기록한다. */
	free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
	if (free_map_file == NULL)
		PANIC ("can't open free map");
	if (!bitmap_write (free_map, free_map_file))
		PANIC ("can't write free map");
}
