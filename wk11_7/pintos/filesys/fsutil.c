#include "filesys/fsutil.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

/*
 * 루트 디렉터리의 파일 목록을 출력한다.
 */
void
fsutil_ls (char **argv UNUSED) {
	struct dir *dir;
	char name[NAME_MAX + 1];

	printf ("Files in the root directory:\n");
	dir = dir_open_root ();
	if (dir == NULL)
		PANIC ("root dir open failed");
	while (dir_readdir (dir, name))
		printf ("%s\n", name);
	printf ("End of listing.\n");
}

/*
 * ARGV[1] 파일의 내용을 시스템 콘솔에 16진수와 ASCII 형태로 출력한다.
 */
void
fsutil_cat (char **argv) {
	const char *file_name = argv[1];

	struct file *file;
	char *buffer;

	printf ("Printing '%s' to the console...\n", file_name);
	file = filesys_open (file_name);
	if (file == NULL)
		PANIC ("%s: open failed", file_name);
	buffer = palloc_get_page (PAL_ASSERT);
	for (;;) {
		off_t pos = file_tell (file);
		off_t n = file_read (file, buffer, PGSIZE);
		if (n == 0)
			break;

		hex_dump (pos, buffer, n, true); 
	}
	palloc_free_page (buffer);
	file_close (file);
}

/*
 * ARGV[1] 파일을 삭제한다.
 */
void
fsutil_rm (char **argv) {
	const char *file_name = argv[1];

	printf ("Deleting '%s'...\n", file_name);
	if (!filesys_remove (file_name))
		PANIC ("%s: delete failed\n", file_name);
}

/*
 * "scratch" 디스크, 즉 hdc 또는 hd1:0에서 파일 시스템의
 * ARGV[1] 파일로 복사한다.
 *
 * scratch 디스크의 현재 섹터는 "PUT\0" 문자열로 시작해야 하며,
 * 그 뒤에는 파일 크기를 바이트 단위로 나타내는 32비트 little-endian
 * 정수가 와야 한다. 그 다음 섹터들에는 파일 내용이 들어 있다.
 *
 * 이 함수를 처음 호출하면 scratch 디스크의 처음부터 읽기 시작한다.
 * 이후 호출에서는 디스크 위의 위치가 앞으로 진행된다.
 * 이 디스크 위치는 fsutil_get()이 사용하는 위치와는 독립적이므로,
 * 모든 `put`은 모든 `get`보다 먼저 와야 한다.
 */
void
fsutil_put (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	struct disk *src;
	struct file *dst;
	off_t size;
	void *buffer;

	printf ("Putting '%s' into the file system...\n", file_name);

	/*
	 * 버퍼를 할당한다.
	 */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/*
	 * 원본 디스크를 열고 파일 크기를 읽을 준비를 한다.
	 */
	src = disk_get (1, 0);
	if (src == NULL)
		PANIC ("couldn't open source disk (hdc or hd1:0)");

	/*
	 * 파일 크기를 읽는다.
	 */
	disk_read (src, sector++, buffer);
	if (memcmp (buffer, "PUT", 4))
		PANIC ("%s: missing PUT signature on scratch disk", file_name);
	size = ((int32_t *) buffer)[1];
	if (size < 0)
		PANIC ("%s: invalid file size %d", file_name, size);

	/*
	 * 대상 파일을 만든다.
	 */
	if (!filesys_create (file_name, size))
		PANIC ("%s: create failed", file_name);
	dst = filesys_open (file_name);
	if (dst == NULL)
		PANIC ("%s: open failed", file_name);

	/*
	 * 실제 복사를 수행한다.
	 */
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		disk_read (src, sector++, buffer);
		if (file_write (dst, buffer, chunk_size) != chunk_size)
			PANIC ("%s: write failed with %"PROTd" bytes unwritten",
					file_name, size);
		size -= chunk_size;
	}

	/*
	 * 마무리한다.
	 */
	file_close (dst);
	free (buffer);
}

/*
 * 파일 시스템의 FILE_NAME 파일을 scratch 디스크로 복사한다.
 *
 * scratch 디스크의 현재 섹터에는 "GET\0"이 기록되고,
 * 그 뒤에는 파일 크기를 바이트 단위로 나타내는 32비트
 * little-endian 정수가 기록된다. 이후 섹터들에는 파일 데이터가 기록된다.
 *
 * 이 함수를 처음 호출하면 scratch 디스크의 처음부터 쓰기 시작한다.
 * 이후 호출에서는 디스크 위의 위치가 앞으로 진행된다.
 * 이 디스크 위치는 fsutil_put()이 사용하는 위치와는 독립적이므로,
 * 모든 `put`은 모든 `get`보다 먼저 와야 한다.
 */
void
fsutil_get (char **argv) {
	static disk_sector_t sector = 0;

	const char *file_name = argv[1];
	void *buffer;
	struct file *src;
	struct disk *dst;
	off_t size;

	printf ("Getting '%s' from the file system...\n", file_name);

	/*
	 * 버퍼를 할당한다.
	 */
	buffer = malloc (DISK_SECTOR_SIZE);
	if (buffer == NULL)
		PANIC ("couldn't allocate buffer");

	/*
	 * 원본 파일을 연다.
	 */
	src = filesys_open (file_name);
	if (src == NULL)
		PANIC ("%s: open failed", file_name);
	size = file_length (src);

	/*
	 * 대상 디스크를 연다.
	 */
	dst = disk_get (1, 0);
	if (dst == NULL)
		PANIC ("couldn't open target disk (hdc or hd1:0)");

	/*
	 * 섹터 0에 파일 크기를 기록한다.
	 */
	memset (buffer, 0, DISK_SECTOR_SIZE);
	memcpy (buffer, "GET", 4);
	((int32_t *) buffer)[1] = size;
	disk_write (dst, sector++, buffer);

	/*
	 * 실제 복사를 수행한다.
	 */
	while (size > 0) {
		int chunk_size = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;
		if (sector >= disk_size (dst))
			PANIC ("%s: out of space on scratch disk", file_name);
		if (file_read (src, buffer, chunk_size) != chunk_size)
			PANIC ("%s: read failed with %"PROTd" bytes unread", file_name, size);
		memset (buffer + chunk_size, 0, DISK_SECTOR_SIZE - chunk_size);
		disk_write (dst, sector++, buffer);
		size -= chunk_size;
	}

	/*
	 * 마무리한다.
	 */
	file_close (src);
	free (buffer);
}
