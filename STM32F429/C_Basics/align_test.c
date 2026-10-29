/**
 * align_test.c —— 结构体内存对齐实证
 *
 * 用 offsetof 把每个成员的真实偏移打出来，看空洞在哪。
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>      /* offsetof */

typedef struct { uint8_t a; uint8_t  b; uint16_t c; } S1;
typedef struct { uint8_t a; uint16_t b; uint8_t  c; } S2;
typedef struct { uint32_t a; uint8_t b; }             S3;
typedef struct { uint8_t a; uint32_t b; uint8_t c; }  S4;

static void dump(const char *name, size_t sz,
                 const char *m1, size_t o1, size_t s1,
                 const char *m2, size_t o2, size_t s2,
                 const char *m3, size_t o3, size_t s3)
{
    size_t i;
    size_t member[3] = { s1, s2, s3 };

    printf("--- %s : sizeof = %u 字节 ---\n", name, (unsigned)sz);
    printf("  偏移: %s@%u(%u)  %s@%u(%u)  %s@%u(%u)\n",
           m1, (unsigned)o1, (unsigned)s1,
           m2, (unsigned)o2, (unsigned)s2,
           m3, (unsigned)o3, (unsigned)s3);

    /* 逐字节画出来：哪一格是空洞 */
    printf("  [ ");
    for (i = 0; i < sz; i++)
    {
        if (i == o1)                       printf("%s ", m1);
        else if (i == o2)                  printf("%s ", m2);
        else if (m3 && i == o3)            printf("%s ", m3);
        else                               printf("__ ");
        (void)member;
    }
    printf("]\n\n");
}

int main(void)
{
    dump("S1 {u8 a; u8 b; u16 c}",
         sizeof(S1),
         "a", offsetof(S1,a), sizeof(((S1*)0)->a),
         "b", offsetof(S1,b), sizeof(((S1*)0)->b),
         "c", offsetof(S1,c), sizeof(((S1*)0)->c));

    dump("S2 {u8 a; u16 b; u8 c}",
         sizeof(S2),
         "a", offsetof(S2,a), sizeof(((S2*)0)->a),
         "b", offsetof(S2,b), sizeof(((S2*)0)->b),
         "c", offsetof(S2,c), sizeof(((S2*)0)->c));

    dump("S3 {u32 a; u8 b}",
         sizeof(S3),
         "a", offsetof(S3,a), sizeof(((S3*)0)->a),
         "b", offsetof(S3,b), sizeof(((S3*)0)->b),
         "",  0, 0);

    dump("S4 {u8 a; u32 b; u8 c}",
         sizeof(S4),
         "a", offsetof(S4,a), sizeof(((S4*)0)->a),
         "b", offsetof(S4,b), sizeof(((S4*)0)->b),
         "c", offsetof(S4,c), sizeof(((S4*)0)->c));

    return 0;
}
