/**
 * struct_demo.c —— 结构体指针：`. ` 和 `->` 到底差在哪
 *
 * 用法：先预测输出，再运行对比。
 *
 * 编译：
 *   gcc -std=c11 -Wall -Wextra -o struct_demo.exe struct_demo.c
 *   .\struct_demo.exe
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * 定义一个结构体
 *   「struct」是类型的关键字，Point 是这个类型的名字
 *   typedef 让后面可以只写 Point 而不用写 struct Point
 * ========================================================================== */
typedef struct
{
    uint8_t  id;        /* 1 字节 */
    uint16_t value;     /* 2 字节 */
} Point;

/* ============================================================================
 * 场景 1：传整个结构体（按值传）
 *
 * 函数拿到的是【一份拷贝】—— 和 uint8_t 传值改不动是一个道理，
 * 只不过拷贝的是整个结构体的所有成员。
 * ========================================================================== */
void bump_by_value(Point p)
{
    p.value = 999;
}

/* ============================================================================
 * 场景 2：传结构体指针（按址传）
 *
 * 这才是实际写代码的方式：
 *   - 改得动原结构体
 *   - 不用拷贝整个结构体（结构体大时省很多时间和栈空间）
 * ========================================================================== */
void bump_by_ptr(Point *p)
{
    /* p->value  等价于  (*p).value */
    p->value = 999;
}

/* ============================================================================
 * 场景 3：真实代码的样子 —— 初始化函数
 *
 * 这就是 HAL_GPIO_Init(GPIOH, &g) 那个模式。
 * ========================================================================== */
void point_set(Point *p, uint8_t id, uint16_t value)
{
    p->id    = id;
    p->value = value;
}

int main(void)
{
    Point  a;           /* 结构体变量本身 */
    Point *pa;          /* 指向 Point 的指针 */
    Point  arr[3];      /* 结构体数组 */
    uint16_t i;

    /* ---------- 场景 1 ---------- */
    printf("===== 场景 1：传整个结构体 =====\n");
    a.id = 1; a.value = 100;
    bump_by_value(a);
    printf("a.value = %u      <-- 预测？\n", (unsigned)a.value);

    /* ---------- 场景 2 ---------- */
    printf("\n===== 场景 2：传结构体指针 =====\n");
    a.id = 1; a.value = 100;
    bump_by_ptr(&a);                  /* 注意 & */
    printf("a.value = %u      <-- 预测？\n", (unsigned)a.value);

    /* ---------- 场景 3 ---------- */
    printf("\n===== 场景 3：点号 vs 箭头 =====\n");
    pa = &a;                          /* pa 指向 a */

    pa->id    = 7;                    /* 用指针访问成员：箭头 */
    printf("pa->id    = %u\n", (unsigned)pa->id);
    printf("a.id      = %u   <-- 和上面一样吗？\n", (unsigned)a.id);

    (*pa).value = 555;                /* 等价写法：先解引用再点 */
    printf("(*pa).value = %u\n", (unsigned)(*pa).value);
    printf("a.value     = %u   <-- 和上面一样吗？\n", (unsigned)a.value);

    /* ---------- 场景 4：结构体数组 ---------- */
    printf("\n===== 场景 4：结构体数组 + 指针遍历 =====\n");
    for (i = 0; i < 3; i++)
    {
        arr[i].id    = (uint8_t)i;
        arr[i].value = (uint16_t)(i * 10);
    }

    {
        Point *p = arr;               /* 指向第一个元素 */
        for (i = 0; i < 3; i++)
        {
            printf("arr[%u] id=%u value=%u\n",
                   (unsigned)i, (unsigned)p->id, (unsigned)p->value);
            p++;                      /* 挪到下一个结构体 */
        }
    }

    /* ---------- 场景 5：真实写法 ---------- */
    printf("\n===== 场景 5：初始化函数 =====\n");
    point_set(&a, 42, 1234);
    printf("a.id=%u a.value=%u\n", (unsigned)a.id, (unsigned)a.value);

    /* ---------- 场景 6：结构体有多大 ---------- */
    printf("\n===== 场景 6：sizeof =====\n");
    printf("sizeof(Point) = %u 字节\n", (unsigned)sizeof(Point));
    printf("  成员是 uint8_t(1) + uint16_t(2) = 3 字节，为什么不是 3？\n");

    return 0;
}
