#include "kernel/types.h"
#include "user.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"

char *testname;

#define fail(s) _fail(s, __LINE__)

void
_fail(char *s, int line)
{
	fprintf(2, "%s failed on line %d: %s\n", testname, line, s);
	exit();
}

// Perform a test that should crash the process.
// fork() ensures that our main process survives.
void
control(void (*fun)(void), char *failstr)
{
	switch (fork()) {
	case -1:
		fail("fork");
		break;
	case 0:
		fun();
		fail(failstr);
		break;
	default:
		wait();
		break;
	}
}

char *target;

// Try to write into the target location in memory.
// This is a convenient function to pass into control().
void
trywrite(void)
{
	strncpy(target, "abcdefgh", 8);
}

// Test mmap() with MAP_ANONYMOUS; test clean munmap().
void
anontest(void)
{
	void *addr;
	int ret;
	testname = "anontest";

	target = (char*)(KERNBASE - 0x1100);
	control(trywrite, "control 1 succeeded?");

	addr = mmap(0, 0x4000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	if (addr == MAP_FAILED)
		fail("mmap");

	target = addr;
	trywrite();

	// control
	ret = munmap((void*)0x4000, 0x4000);
	if (ret != -1)
		fail("munmap control succeeded?");

	ret = munmap(addr, 0x4000);
	if (ret < 0)
		fail("munmap");

	// was it really unmapped?
	control(trywrite, "control 2 succeeded?");

	printf("anontest done.\n");
}

// Test mmap() at a fixed address. 
// Test partial munmap().
void
munmaptest(void)
{
	void *addr;
	void *hint;
	int ret;
	testname = "munmaptest";

	// this address should be ok to map to
	hint = (void*)0x00f00000;
	addr = mmap(hint, 0xf000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	if (addr == MAP_FAILED)
		fail("mmap");
	else if (addr != hint)
		fail("hint");

	ret = munmap(addr, 0x3000);
	if (ret < 0)
		fail("munmap start");

	target = (char*)0x00f01000;
	control(trywrite, "control 1 succeeded?");
	strncpy((char*)0x00f04000, "abcdefgh", 8);

	ret = munmap(addr+0xe000, 0x2000);
	if (ret < 0)
		fail("munmap end");

	target = (char*)0x00f0e000;
	control(trywrite, "control 2 succeeded?");
	strncpy((char*)0x00f0a000, "abcdefgh", 8);

	ret = munmap(addr+0x8000, 0x2000);
	if (ret < 0)
		fail("munmap middle");

	strncpy((char*)0x00f05000, "abcdefgh", 8);
	target = (char*)0x00f09000;
	control(trywrite, "control 3 succeeded?");
	strncpy((char*)0x00f0c000, "abcdefgh", 8);

	// cleanup.
	ret = munmap(addr+0x3000, 0x4000);
	if (ret < 0)
		fail("cleanup");
	ret = munmap(addr+0xb000, 0x3000);
	if (ret < 0)
		fail("cleanup");

	printf("munmaptest done.\n");
}

// Check if mmap() properly rejects unaligned length arguments.
void
alignmenttest(void)
{
	void *addr;
	testname = "alignmenttest";

	addr = mmap(0, 0x1337, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	if (addr != MAP_FAILED)
		fail("mmap");

	printf("alignmenttest done.\n");
}

// Test mapping files into memory, as well as reading from and
// writing into them.
void
filetest(void)
{
	void *addr;
	int i, fd, ret;
	char buf[128];
	testname = "filetest";

	// create test file
	fd = open("/home/mapfile", O_RDWR|O_CREATE);
	if (fd < 0)
		fail("open");

	memset(buf, 'A', 128);
	buf[127] = 0;

	ret = write(fd, buf, 128);
	if (ret != 128)
		fail("write");

	addr = mmap(0, 0x1000, PROT_READ|PROT_WRITE, 0, fd, 0);
	if (addr == MAP_FAILED)
		fail("mmap");

	if (strcmp(addr, buf) != 0)
		fail("file contents");

	if (((char*)addr)[200] != 0)
		fail("padding zeroes");

	memset(addr, 'B', 127);

	ret = msync(addr, 0x1000);
	if (ret < 0)
		fail("msync");

	ret = munmap(addr, 0x1000);
	if (ret < 0)
		fail("munmap");

	// reset offset. we don't have lseek().
	close(fd);

	fd = open("/home/mapfile", O_RDONLY);
	if (fd < 0)
		fail("open");

	ret = read(fd, buf, 127);
	if (ret < 0)
		fail("read");

	for (i = 0; i < 127; i++)
		if (buf[i] != 'B')
			fail("modified contents");

	close(fd);
	unlink("/home/mapfile");

	printf("filetest done.\n");
}

// Helper funtion that writes into a shared memory object
// in a different process.
void
_shmtest_write(int fd)
{
	int ret;
	void *addr;

	switch (fork()) {
	case -1:
		fail("fork");
	case 0:
		// execute the rest of this function
		break;
	default:
		wait();
		return;
	}

	addr = mmap(0, 0x2000, PROT_READ|PROT_WRITE, 0, fd, 0);
	if (addr == MAP_FAILED)
		fail("mmap");

	// test crossing the page boundary.
	memset(addr+0x0ff0, 'A', 256);

	ret = munmap(addr, 0x2000);
	if (ret < 0)
		fail("munmap");

	exit();
}

// Helper function that reads from a shared memory object
// in a different process. Also tests page fault on write
// without PROT_WRITE.
void
_shmtest_read(int fd)
{
	int ret, i;
	void *addr;

	switch (fork()) {
	case -1:
		fail("fork");
	case 0:
		// execute the rest of this function
		break;
	default:
		wait();
		return;
	}

	// notice the lack of PROT_WRITE.
	addr = mmap(0, 0x2000, PROT_READ, 0, fd, 0);
	if (addr == MAP_FAILED)
		fail("mmap");

	for (i = 0; i < 256; i++)
		if (((char*)addr)[i+0x0ff0] != 'A')
			fail("shmem content");

	target = addr;
	control(trywrite, "writing without PROT_WRITE succeeded?");

	ret = munmap(addr, 0x2000);
	if (ret < 0)
		fail("munmap");

	exit();
}

// Test mapping shared memory segments.
void
shmtest(void)
{
	int fd, ret;
	testname = "shmtest";

	fd = shm_open("noslash", O_RDWR|O_CREATE);
	if (fd != -1)
		fail("shm name must start with a slash");

	fd = shm_open("/multi/slash", O_RDWR|O_CREATE);
	if (fd != -1)
		fail("shm name must have only one slash");

	fd = shm_open("/testmem", O_RDWR|O_CREATE);
	if (fd < 0)
		fail("shm_open");

	ret = ftruncate(fd, 0x2000);
	if (ret < 0)
		fail("ftruncate");

	_shmtest_write(fd);
	_shmtest_read(fd);

	ret = shm_stat(fd);
	if (ret != 1) // only _shmtest_write()
		fail("shm_stat");

	close(fd);

	ret = shm_unlink("/testmem");
	if (ret < 0)
		fail("shm_unlink");

	printf("shmtest done.\n");
}

int
main(void)
{
	anontest();
	munmaptest();
	alignmenttest();
	filetest();
	shmtest();

	printf("ALL TESTS PASSED.\n");
	exit();
}
