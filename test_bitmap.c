#include <stdio.h>
#include "heap.h"
#include <assert.h>
#include <string.h>

#define BITMAP192
#define THREAD_NUM 20
#define TEST_NUM   1000
#define INC_MASK   3
int random_array[80] = {48, 65, 76, 9, 75, 54, 8, 54, 13, 10, 10, 80, 56, 26, 29, 61, 60, 80, 33, 16, 18, 36, 64, 54, 37, 70, 75, 63, 41, 63, 12, 56, 80, 33, 68, 60, 39, 36, 46, 73, 21, 44, 54, 76, 18, 50, 49, 59, 7, 31, 74, 66, 32, 37, 58, 10, 58, 77, 69, 66, 52, 10, 68, 67, 80, 57, 13, 42, 56, 15, 26, 11, 73, 36, 20, 32, 4, 28, 51, 28};
#ifdef BITMAP192
unsigned long bm[6];
int dummy_queue[192];
#else
int dummy_queue[96];
unsigned long bm[3];
#endif
int random_inc[4] = {20, 30, 50, 60};
int to_sched[THREAD_NUM];
int base;
int to_sched_hdr;

/*struct test {
	int deadline;
	int coreid;
	int thdid;
	int offset;
};

struct test *pointer;*/

struct test {
	int idx;
	int val;
};

struct test_heap {
	struct heap h;
	void  *data[100];
	char   pad;
} test_heap;

static struct heap *hs = (struct heap *)&test_heap;
static struct test test_array[THREAD_NUM];

void
ben_init(void)
{
#ifdef BITMAP192
	memset(bm, 0, sizeof(unsigned long) * 6);
	memset(dummy_queue, 0, sizeof(int) * 192);
#else
	memset(dummy_queue, 0, sizeof(int) * 96);
	memset(bm, 0, sizeof(unsigned long) * 3);
#endif
	memset(to_sched, 0, sizeof(int) * THREAD_NUM);
	base = 0;
	to_sched_hdr = 0;
}

static int
__compare_min(void *a, void *b)
{
	return ((struct test *)a) -> val <= ((struct test *)b) ->val;
}

static void
__update_idx(void *e, int pos)
{
	((struct test *)e) -> idx = pos;
}

static inline int
__find_first_bit(unsigned long *bm)
{
	if (bm[0])
		return __builtin_ctzl(bm[0]);
	if (bm[1])
		return (__builtin_ctzl(bm[1]) + 32);
#ifdef BITMAP192
	if (bm[2])
		return (__builtin_ctzl(bm[2]) + 64);
	if (bm[3])
		return (__builtin_ctzl(bm[3]) + 96);
	if (bm[4])
		return (__builtin_ctzl(bm[4]) + 128);
	return (__builtin_ctzl(bm[5]) + 160);
#else
	return (__builtin_ctzl(bm[2]) + 64);
#endif
}

static inline unsigned long
test_tsc(void)
{
	unsigned long a, d, c;

	__asm__ __volatile("rdtsc" : "=a" (a), "=d" (d), "=c" (c) : : );

	return ((unsigned long long)d << 32) | (unsigned long)a;
}

static inline void
__set_bit(int pos)
{
#ifdef BITMAP192
	assert(pos < 192);
#else
	assert(pos < 96);
#endif
	if (pos < 32) {
		bm[0] |= (0x1 << pos);
		return;
	}
	if (pos < 64) {
		bm[1] |= (0x1 << (pos - 32));
		return;
	}
#ifdef BITMAP192
	if (pos < 96) {
		bm[2] |= (0x1 << (pos - 64));
		return;
	}
	if (pos < 128) {
		bm[3] |= (0x1 << (pos - 96));
		return;
	}
	if (pos < 160) {
		bm[4] |= (0x1 << (pos - 128));
		return;
	}
	bm[5] |= (0x1 << (pos - 160));
#else
	bm[2] |= (0x1 << (pos - 64));
#endif
	return;
}

static inline void
__clear_bit(int pos)
{
	//printf("clearbit\n");
#ifdef BITMAP192
	assert(pos < 192);
#else
	assert(pos < 96);
#endif
	if (pos < 32) {
		bm[0] &= ~(0x1 << pos);
		return;
	}
	if (pos < 64) {
		bm[1] &= ~(0x1 << (pos - 32));
		return;
	}
#ifdef BITMAP192
	if (pos < 96) {
		bm[2] &= ~(0x1 << (pos - 64));
		return;
	}
	if (pos < 128) {
		bm[3] &= ~(0x1 << (pos - 96));
		return;
	}
	if (pos < 160) {
		bm[4] &= ~(0x1 << (pos - 128));
		return;
	}
	bm[5] &= ~(0x1 << (pos - 160));
	return;
#else
	bm[2] &= ~(0x1 << (pos - 64));
#endif
	return;
}

void
bitmap_init()
{
	int i = 0;
	int pos;
	for (i = 0; i < THREAD_NUM; i++) {
		pos = random_array[i] - base;
		__set_bit(pos);
		dummy_queue[pos] ++;
	}
}

static inline void
__load_sched(void) {
	int i = 0, pos;

#ifdef BITMAP192
	base = base + 192;
#else
	base = base + 96;
#endif
	for (i = 0; i < to_sched_hdr; i++) {
		pos = to_sched[i] - base;
		//assert(pos >= 0 && pos < 96);
		__set_bit(pos);
		//printf("\tto_sched_hdr: %d\n", to_sched_hdr);
	}
	//assert(to_sched == 0);
	to_sched_hdr = 0;
}

static inline void
heap_pre_load(void) {
	int i = 0;

	for ( i = 0; i < THREAD_NUM; i++) {
		test_array[i].val = random_array[i];
		test_array[i].idx = -1;
		heap_add(hs, &test_array[i]);
	}
}

int
main(void) {
	unsigned long long start, end, res;
	int i = 0, ret, next;
	struct test *t;

	ben_init();
	ret = __find_first_bit(bm);
	//printf("get ret when empty: %d\n", ret);
	bitmap_init();
	/* for bitmap test */
	printf("bitmap test result:\n");
	start = test_tsc();
	/*for (i = 0; i < 80; i++) {
		if (random_array[i] < 32) {
			bm[0] = bm[0] | (0x1 << random_array[i]);
			continue;
		}
		if (random_array[i] < 64) {
			bm[1] = bm[1] | (0x1 << (random_array[i] - 32));
			continue;
		}
		bm[2] = bm[2] | (0x1 << (random_array[i] - 64));
	}*/
	i = 0;
	while (i < TEST_NUM) {
		ret = __find_first_bit(bm);
		dummy_queue[ret]--;
		if (dummy_queue[ret] == 0)
			__clear_bit(ret);
#ifdef BITMAP192
		if (ret == 192) {
#else
		if (ret == 96) {
#endif
			__load_sched();
		}
		next = ret + base + random_inc[i & INC_MASK];
#ifdef BITMAP192
		if (next - base < 192) {
#else
		if (next - base < 96) {
#endif
			__set_bit(next - base);
			dummy_queue[next - base]++;
		} else {
			to_sched[to_sched_hdr] = next;
			to_sched_hdr++;
		}
		i++;
	}
	end = test_tsc();
	res = end - start;
	printf("\ttotal experiment time: %lld cycles, average experiment time: %lld cycles per entry\n", res, res/TEST_NUM);

	//start = test_tsc();
	//int ret = __find_first_bit(bm);
	//end = test_tsc();

	//res = end - start;

	//printf("\tres: %d, time used: %lld cycles\n", ret, res);
	//printf("####### len of structure: %d, len of int: %d, len of long: %d, len of longlong:%d, len char: %d\n", sizeof(pointer), sizeof(int), sizeof(unsigned long), sizeof(unsigned long long), sizeof(char *));

	/* for heap test */
	heap_init(hs, 100, __compare_min, __update_idx);
	heap_pre_load();
	/*start = test_tsc();
	for (i = 0; i < 80; i++) {
		test_array[i].val = random_array[i];
		test_array[i].idx = -1;
		heap_add(hs, &test_array[i]);
	}
	end = test_tsc();
	res = end - start;
	printf("heap test result:\n");
	printf("\ttotal insert time: %lld cycles, average insert time: %lld cycles per entry\n", res, res/80);
*/
	start = test_tsc();
	i = 0;
	while (i < TEST_NUM) {
		t = heap_peek(hs);
		heap_remove(hs, t->idx);
		t->val += random_inc[i & INC_MASK];
		t->idx = -1;
		heap_add(hs, t);
		i++;
	}
	//struct test *t = heap_peek(hs);
	end = test_tsc();
	res = end - start;

	printf("\ttime used total: %llu, time used per request: %llu\n", res, res/TEST_NUM);

	return 0;
}
