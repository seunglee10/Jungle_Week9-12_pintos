#include "devices/disk.h"
#include <ctype.h>
#include <debug.h>
#include <stdbool.h>
#include <stdio.h>
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

/* 이 파일의 코드는 ATA(IDE) 컨트롤러를 다루는 인터페이스다.
   [ATA-3] 규격을 따르도록 작성되었다. */

/* ATA 명령 블록 포트 주소. */
#define reg_data(CHANNEL) ((CHANNEL)->reg_base + 0)     /* 데이터. */
#define reg_error(CHANNEL) ((CHANNEL)->reg_base + 1)    /* 오류. */
#define reg_nsect(CHANNEL) ((CHANNEL)->reg_base + 2)    /* 섹터 수. */
#define reg_lbal(CHANNEL) ((CHANNEL)->reg_base + 3)     /* LBA 0:7. */
#define reg_lbam(CHANNEL) ((CHANNEL)->reg_base + 4)     /* LBA 15:8. */
#define reg_lbah(CHANNEL) ((CHANNEL)->reg_base + 5)     /* LBA 23:16. */
#define reg_device(CHANNEL) ((CHANNEL)->reg_base + 6)   /* 장치/LBA 27:24. */
#define reg_status(CHANNEL) ((CHANNEL)->reg_base + 7)   /* 상태 (읽기 전용). */
#define reg_command(CHANNEL) reg_status (CHANNEL)       /* 명령 (쓰기 전용). */

/* ATA 제어 블록 포트 주소.
   (비레거시 ATA 컨트롤러까지 지원한다면 충분히 유연하지 않겠지만,
   지금 구현 범위에서는 이 정도면 충분하다.) */
#define reg_ctl(CHANNEL) ((CHANNEL)->reg_base + 0x206)  /* 제어 (쓰기 전용). */
#define reg_alt_status(CHANNEL) reg_ctl (CHANNEL)       /* 보조 상태 (읽기 전용). */

/* 보조 상태 레지스터 비트. */
#define STA_BSY 0x80            /* 사용 중. */
#define STA_DRDY 0x40           /* 장치 준비 완료. */
#define STA_DRQ 0x08            /* 데이터 요청. */

/* 제어 레지스터 비트. */
#define CTL_SRST 0x04           /* 소프트웨어 리셋. */

/* 장치 레지스터 비트. */
#define DEV_MBS 0xa0            /* 반드시 설정되어야 함. */
#define DEV_LBA 0x40            /* 선형 기반 주소 지정. */
#define DEV_DEV 0x10            /* 장치 선택: 0=주 장치, 1=보조 장치. */

/* 명령어.
   정의된 것은 훨씬 많지만, 여기서는 필요한 일부만 사용한다. */
#define CMD_IDENTIFY_DEVICE 0xec        /* 장치 식별 명령. */
#define CMD_READ_SECTOR_RETRY 0x20      /* 재시도를 포함한 섹터 읽기. */
#define CMD_WRITE_SECTOR_RETRY 0x30     /* 재시도를 포함한 섹터 쓰기. */

/* ATA 장치. */
struct disk {
	char name[8];               /* 이름. 예: "hd0:1". */
	struct channel *channel;    /* 이 디스크가 연결된 채널. */
	int dev_no;                 /* master/slave에 해당하는 장치 번호 0 또는 1. */

	bool is_ata;                /* 1이면 이 장치는 ATA 디스크. */
	disk_sector_t capacity;     /* 용량(섹터 단위, is_ata인 경우). */

	long long read_cnt;         /* 읽은 섹터 수. */
	long long write_cnt;        /* 쓴 섹터 수. */
};

/* ATA 채널(컨트롤러라고도 부름).
   각 채널은 최대 두 개의 디스크를 제어할 수 있다. */
struct channel {
	char name[8];               /* 이름. 예: "hd0". */
	uint16_t reg_base;          /* 기본 I/O 포트. */
	uint8_t irq;                /* 사용 중인 인터럽트. */

	struct lock lock;           /* 컨트롤러에 접근하려면 반드시 획득. */
	bool expecting_interrupt;   /* 예상한 인터럽트면 true, 아니면 가짜 인터럽트. */
	struct semaphore completion_wait;   /* 인터럽트 핸들러가 up 한다. */

	struct disk devices[2];     /* 이 채널에 연결된 장치들. */
};

/* 표준 PC에서 볼 수 있는 두 개의 "레거시" ATA 채널을 지원한다. */
#define CHANNEL_CNT 2
static struct channel channels[CHANNEL_CNT];

static void reset_channel (struct channel *);
static bool check_device_type (struct disk *);
static void identify_ata_device (struct disk *);

static void select_sector (struct disk *, disk_sector_t);
static void issue_pio_command (struct channel *, uint8_t command);
static void input_sector (struct channel *, void *);
static void output_sector (struct channel *, const void *);

static void wait_until_idle (const struct disk *);
static bool wait_while_busy (const struct disk *);
static void select_device (const struct disk *);
static void select_device_wait (const struct disk *);

static void interrupt_handler (struct intr_frame *);

/* 디스크 하위 시스템을 초기화하고 디스크를 탐지한다. */
void
disk_init (void) {
	size_t chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		struct channel *c = &channels[chan_no];
		int dev_no;

		/* 채널 초기화. */
		snprintf (c->name, sizeof c->name, "hd%zu", chan_no);
		switch (chan_no) {
			case 0:
				c->reg_base = 0x1f0;
				c->irq = 14 + 0x20;
				break;
			case 1:
				c->reg_base = 0x170;
				c->irq = 15 + 0x20;
				break;
			default:
				NOT_REACHED ();
		}
		lock_init (&c->lock);
		c->expecting_interrupt = false;
		sema_init (&c->completion_wait, 0);

		/* 장치 초기화. */
		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = &c->devices[dev_no];
			snprintf (d->name, sizeof d->name, "%s:%d", c->name, dev_no);
			d->channel = c;
			d->dev_no = dev_no;

			d->is_ata = false;
			d->capacity = 0;

			d->read_cnt = d->write_cnt = 0;
		}

		/* 인터럽트 핸들러 등록. */
		intr_register_ext (c->irq, interrupt_handler, c->name);

		/* 하드웨어 리셋. */
		reset_channel (c);

		/* ATA 하드디스크와 다른 장치를 구분. */
		if (check_device_type (&c->devices[0]))
			check_device_type (&c->devices[1]);

		/* 하드디스크 식별 정보 읽기. */
		for (dev_no = 0; dev_no < 2; dev_no++)
			if (c->devices[dev_no].is_ata)
				identify_ata_device (&c->devices[dev_no]);
	}

	/* 아래 줄은 수정하지 말 것. */
	register_disk_inspect_intr ();
}

/* 디스크 통계를 출력한다. */
void
disk_print_stats (void) {
	int chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		int dev_no;

		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = disk_get (chan_no, dev_no);
			if (d != NULL && d->is_ata)
				printf ("%s: %lld reads, %lld writes\n",
						d->name, d->read_cnt, d->write_cnt);
		}
	}
}

/* CHAN_NO번 채널 안에서 DEV_NO번 디스크를 반환한다.
   DEV_NO는 주 장치면 0, 보조 장치면 1이다.

   Pintos는 디스크를 다음과 같이 사용한다.
0:0 - 부트 로더, 커널 명령줄 인자, 운영체제 커널
0:1 - 파일 시스템
1:0 - scratch
1:1 - swap
*/
struct disk *
disk_get (int chan_no, int dev_no) {
	ASSERT (dev_no == 0 || dev_no == 1);

	if (chan_no < (int) CHANNEL_CNT) {
		struct disk *d = &channels[chan_no].devices[dev_no];
		if (d->is_ata)
			return d;
	}
	return NULL;
}

/* 디스크 D의 크기를 DISK_SECTOR_SIZE 바이트 섹터 개수로 반환한다. */
disk_sector_t
disk_size (struct disk *d) {
	ASSERT (d != NULL);

	return d->capacity;
}

/* 디스크 D의 SEC_NO 섹터를 BUFFER로 읽는다.
   BUFFER는 DISK_SECTOR_SIZE 바이트를 담을 수 있어야 한다.
   디스크 접근 동기화는 내부에서 처리하므로 외부의 디스크별 락은 필요 없다. */
void
disk_read (struct disk *d, disk_sector_t sec_no, void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_READ_SECTOR_RETRY);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d))
		PANIC ("%s: disk read failed, sector=%"PRDSNu, d->name, sec_no);
	input_sector (c, buffer);
	d->read_cnt++;
	lock_release (&c->lock);
}

/* BUFFER의 내용을 디스크 D의 SEC_NO 섹터에 쓴다.
   BUFFER는 DISK_SECTOR_SIZE 바이트를 담고 있어야 한다.
   디스크가 데이터를 받았다고 확인한 뒤 반환한다.
   디스크 접근 동기화는 내부에서 처리하므로 외부의 디스크별 락은 필요 없다. */
void
disk_write (struct disk *d, disk_sector_t sec_no, const void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_WRITE_SECTOR_RETRY);
	if (!wait_while_busy (d))
		PANIC ("%s: disk write failed, sector=%"PRDSNu, d->name, sec_no);
	output_sector (c, buffer);
	sema_down (&c->completion_wait);
	d->write_cnt++;
	lock_release (&c->lock);
}

/* 디스크 탐지와 식별. */

static void print_ata_string (char *string, size_t size);

/* ATA 채널을 리셋하고, 그 채널에 존재하는 장치들이
   리셋을 끝낼 때까지 기다린다. */
static void
reset_channel (struct channel *c) {
	bool present[2];
	int dev_no;

	/* ATA 리셋 순서는 연결된 장치에 따라 달라지므로,
	   먼저 장치가 있는지부터 확인한다. */
	for (dev_no = 0; dev_no < 2; dev_no++) {
		struct disk *d = &c->devices[dev_no];

		select_device (d);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		outb (reg_nsect (c), 0xaa);
		outb (reg_lbal (c), 0x55);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		present[dev_no] = (inb (reg_nsect (c)) == 0x55
				&& inb (reg_lbal (c)) == 0xaa);
	}

	/* 소프트 리셋 시퀀스를 실행한다.
	   부수 효과로 장치 0이 선택되며, 인터럽트도 함께 활성화된다. */
	outb (reg_ctl (c), 0);
	timer_usleep (10);
	outb (reg_ctl (c), CTL_SRST);
	timer_usleep (10);
	outb (reg_ctl (c), 0);

	timer_msleep (150);

	/* 장치 0의 BSY 비트가 내려갈 때까지 대기. */
	if (present[0]) {
		select_device (&c->devices[0]);
		wait_while_busy (&c->devices[0]);
	}

	/* 장치 1의 BSY 비트가 내려갈 때까지 대기. */
	if (present[1]) {
		int i;

		select_device (&c->devices[1]);
		for (i = 0; i < 3000; i++) {
			if (inb (reg_nsect (c)) == 1 && inb (reg_lbal (c)) == 1)
				break;
			timer_msleep (10);
		}
		wait_while_busy (&c->devices[1]);
	}
}

/* 장치 D가 ATA 디스크인지 확인하고, 결과를 D의 is_ata에 반영한다.
   D가 장치 0(master)라면, 이 채널에 slave(장치 1)가 존재할 가능성이 있으면
   true를 반환한다.
   D가 장치 1(slave)인 경우 반환값에는 의미가 없다. */
static bool
check_device_type (struct disk *d) {
	struct channel *c = d->channel;
	uint8_t error, lbam, lbah, status;

	select_device (d);

	error = inb (reg_error (c));
	lbam = inb (reg_lbam (c));
	lbah = inb (reg_lbah (c));
	status = inb (reg_status (c));

	if ((error != 1 && (error != 0x81 || d->dev_no == 1))
			|| (status & STA_DRDY) == 0
			|| (status & STA_BSY) != 0) {
		d->is_ata = false;
		return error != 0x81;
	} else {
		d->is_ata = (lbam == 0 && lbah == 0) || (lbam == 0x3c && lbah == 0xc3);
		return true;
	}
}

/* 디스크 D에 IDENTIFY DEVICE 명령을 보내고 응답을 읽는다.
   그 결과를 바탕으로 D의 capacity를 초기화하고,
   디스크 정보를 콘솔에 출력한다. */
static void
identify_ata_device (struct disk *d) {
	struct channel *c = d->channel;
	uint16_t id[DISK_SECTOR_SIZE / 2];

	ASSERT (d->is_ata);

	/* IDENTIFY DEVICE 명령을 보낸 뒤,
	   장치 응답이 준비되었다는 인터럽트를 기다리고,
	   데이터를 버퍼로 읽어 온다. */
	select_device_wait (d);
	issue_pio_command (c, CMD_IDENTIFY_DEVICE);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d)) {
		d->is_ata = false;
		return;
	}
	input_sector (c, id);

	/* 용량 계산. */
	d->capacity = id[60] | ((uint32_t) id[61] << 16);

	/* 식별 정보 출력. */
	printf ("%s: detected %'"PRDSNu" sector (", d->name, d->capacity);
	if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024 * 1024)
		printf ("%"PRDSNu" GB",
				d->capacity / (1024 / DISK_SECTOR_SIZE * 1024 * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024)
		printf ("%"PRDSNu" MB", d->capacity / (1024 / DISK_SECTOR_SIZE * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE)
		printf ("%"PRDSNu" kB", d->capacity / (1024 / DISK_SECTOR_SIZE));
	else
		printf ("%"PRDSNu" byte", d->capacity * DISK_SECTOR_SIZE);
	printf (") disk, model \"");
	print_ata_string ((char *) &id[27], 40);
	printf ("\", serial \"");
	print_ata_string ((char *) &id[10], 20);
	printf ("\"\n");
}

/* STRING을 출력한다.
   SIZE 바이트로 이루어져 있으며, 바이트 두 개씩 순서가 뒤집힌 형식이다.
   끝부분의 공백과 null 문자는 출력하지 않는다. */
static void
print_ata_string (char *string, size_t size) {
	size_t i;

	/* 마지막 공백이 아닌 문자이자 null이 아닌 문자를 찾는다. */
	for (; size > 0; size--) {
		int c = string[(size - 1) ^ 1];
		if (c != '\0' && !isspace (c))
			break;
	}

	/* 출력. */
	for (i = 0; i < size; i++)
		printf ("%c", string[i ^ 1]);
}

/* 장치 D를 선택하고 준비될 때까지 기다린 뒤,
   SEC_NO를 디스크의 섹터 선택 레지스터에 기록한다.
   (LBA 모드를 사용한다.) */
static void
select_sector (struct disk *d, disk_sector_t sec_no) {
	struct channel *c = d->channel;

	ASSERT (sec_no < d->capacity);
	ASSERT (sec_no < (1UL << 28));

	select_device_wait (d);
	outb (reg_nsect (c), 1);
	outb (reg_lbal (c), sec_no);
	outb (reg_lbam (c), sec_no >> 8);
	outb (reg_lbah (c), (sec_no >> 16));
	outb (reg_device (c),
			DEV_MBS | DEV_LBA | (d->dev_no == 1 ? DEV_DEV : 0) | (sec_no >> 24));
}

/* 채널 C에 COMMAND를 기록하고,
   완료 인터럽트를 받을 준비를 한다. */
static void
issue_pio_command (struct channel *c, uint8_t command) {
	/* 인터럽트가 켜져 있어야 완료 핸들러가 semaphore를 올릴 수 있다. */
	ASSERT (intr_get_level () == INTR_ON);

	c->expecting_interrupt = true;
	outb (reg_command (c), command);
}

/* PIO 모드에서 채널 C의 데이터 레지스터에서 섹터 하나를 읽어
   SECTOR에 저장한다.
   SECTOR는 DISK_SECTOR_SIZE 바이트를 담을 수 있어야 한다. */
static void
input_sector (struct channel *c, void *sector) {
	insw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* PIO 모드에서 SECTOR를 채널 C의 데이터 레지스터로 기록한다.
   SECTOR는 DISK_SECTOR_SIZE 바이트를 담고 있어야 한다. */
static void
output_sector (struct channel *c, const void *sector) {
	outsw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* 저수준 ATA 기본 동작. */

/* 컨트롤러가 idle 상태가 될 때까지 최대 10초 기다린다.
   즉 상태 레지스터의 BSY와 DRQ 비트가 내려갈 때까지 기다린다.

   부수 효과로, 상태 레지스터를 읽으면 보류 중인 인터럽트가 지워진다. */
static void
wait_until_idle (const struct disk *d) {
	int i;

	for (i = 0; i < 1000; i++) {
		if ((inb (reg_status (d->channel)) & (STA_BSY | STA_DRQ)) == 0)
			return;
		timer_usleep (10);
	}

	printf ("%s: idle timeout\n", d->name);
}

/* 디스크 D의 BSY 비트가 내려갈 때까지 최대 30초 기다린다.
   그 뒤 DRQ 비트 상태를 반환한다.
   ATA 표준에 따르면 디스크 리셋 완료에 이 정도 시간이 걸릴 수 있다. */
static bool
wait_while_busy (const struct disk *d) {
	struct channel *c = d->channel;
	int i;

	for (i = 0; i < 3000; i++) {
		if (i == 700)
			printf ("%s: busy, waiting...", d->name);
		if (!(inb (reg_alt_status (c)) & STA_BSY)) {
			if (i >= 700)
				printf ("ok\n");
			return (inb (reg_alt_status (c)) & STA_DRQ) != 0;
		}
		timer_msleep (10);
	}

	printf ("failed\n");
	return false;
}

/* D가 선택된 디스크가 되도록 D의 채널을 설정한다. */
static void
select_device (const struct disk *d) {
	struct channel *c = d->channel;
	uint8_t dev = DEV_MBS;
	if (d->dev_no == 1)
		dev |= DEV_DEV;
	outb (reg_device (c), dev);
	inb (reg_alt_status (c));
	timer_nsleep (400);
}

/* select_device()처럼 채널 안에서 디스크 D를 선택하되,
   선택 전후로 채널이 idle 상태가 될 때까지 기다린다. */
static void
select_device_wait (const struct disk *d) {
	wait_until_idle (d);
	select_device (d);
	wait_until_idle (d);
}

/* ATA 인터럽트 핸들러. */
static void
interrupt_handler (struct intr_frame *f) {
	struct channel *c;

	for (c = channels; c < channels + CHANNEL_CNT; c++)
		if (f->vec_no == c->irq) {
			if (c->expecting_interrupt) {
				inb (reg_status (c));               /* 인터럽트 수신 확인. */
				sema_up (&c->completion_wait);      /* 대기자 깨우기. */
			} else
				printf ("%s: unexpected interrupt\n", c->name);
			return;
		}

	NOT_REACHED ();
}

static void
inspect_read_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->read_cnt;
}

static void
inspect_write_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->write_cnt;
}

/* 디스크 읽기/쓰기 횟수를 확인하는 테스트용 도구.
 * int 0x43, int 0x44로 호출한다.
 * 입력:
 *   @RDX - 확인할 디스크의 chan_no
 *   @RCX - 확인할 디스크의 dev_no
 * 출력:
 *   @RAX - 디스크의 읽기/쓰기 횟수 */
void
register_disk_inspect_intr (void) {
	intr_register_int (0x43, 3, INTR_OFF, inspect_read_cnt, "Inspect Disk Read Count");
	intr_register_int (0x44, 3, INTR_OFF, inspect_write_cnt, "Inspect Disk Write Count");
}
