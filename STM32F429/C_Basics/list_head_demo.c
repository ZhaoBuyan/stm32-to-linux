/**
 * list_head_demo.c —— 内核风格的双向环形链表（最小可运行版）
 *
 * 演示三件事：
 *   ① 节点嵌在数据里，而不是数据装在节点里
 *   ② 空链表是自己指自己（不是 NULL）
 *   ③ 用 container_of 从"节点地址"反算出"结构体地址"
 *
 * 编译：
 *   gcc -std=c11 -Wall -Wextra -o list_head_demo.exe list_head_demo.c
 *   .\list_head_demo.exe
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>      /* offsetof */

/* ============================================================================
 * 只有两个指针，没有数据
 * ========================================================================== */
struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

/* ============================================================================
 * 从"成员地址"反算"结构体地址"
 *
 *   ptr 是成员在内存里的地址
 *   offsetof(type, member) 是成员相对结构体开头的偏移
 *   ptr - offset 就回到了结构体的开头
 *
 * 这就是你昨天算内存对齐时用的那个 offsetof。
 * ========================================================================== */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/* ============================================================================
 * 空链表：自己指自己
 * ========================================================================== */
static void INIT_LIST_HEAD(struct list_head *h)
{
    h->next = h;
    h->prev = h;
}

/* ============================================================================
 * 把 node 插到 head 后面（成为第一个）
 *
 * ⚠️ 四步顺序不能换：第 ③ 步用的是【还没被改过的】head->next
 * ========================================================================== */
static void list_add(struct list_head *node, struct list_head *head)
{
    node->next       = head->next;      /* ① 自己接上后面 */
    node->prev       = head;            /* ② 自己接上前面 */
    head->next->prev = node;            /* ③ 后面的回头指自己 */
    head->next       = node;            /* ④ head 指自己 */
}

/* ============================================================================
 * 摘掉 node（不需要知道它的前一个是谁 —— node->prev 里有）
 * ========================================================================== */
static void list_del(struct list_head *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;

    /* 摘干净，防止野指针误用（内核里叫 poison） */
    node->next = NULL;
    node->prev = NULL;
}

/* ============================================================================
 * 遍历：从 head->next 出发，绕一圈回到 head 就停
 *   终止条件是 pos != head，不是 pos != NULL
 * ========================================================================== */
#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/* ============================================================================
 * 你的业务数据 —— 链表节点嵌在里面
 * ========================================================================== */
struct device
{
    int             id;
    char            name[8];
    struct list_head list;          /* ← 节点在这里，数据和它平级 */
};

static void dump(const char *tag, struct list_head *head)
{
    struct list_head *pos;

    printf("  %s: [ ", tag);
    list_for_each(pos, head)
    {
        struct device *d = list_entry(pos, struct device, list);
        printf("%d:%s ", d->id, d->name);
    }
    printf("]\n");
}

int main(void)
{
    /* 头节点：不存数据，只当"环的起点" */
    struct list_head head;
    struct device    devA = { 1, "A", { 0, 0 } };
    struct device    devB = { 2, "B", { 0, 0 } };
    struct device    devC = { 3, "C", { 0, 0 } };

    printf("===== 偏移量（container_of 靠它算）=====\n");
    printf("  offsetof(struct device, list) = %u 字节\n",
           (unsigned)offsetof(struct device, list));
    printf("  结构体总大小 sizeof(struct device) = %u 字节\n\n",
           (unsigned)sizeof(struct device));

    printf("===== ① 空链表 =====\n");
    INIT_LIST_HEAD(&head);
    dump("空", &head);

    printf("\n===== ② 依次 add 三个设备 =====\n");
    list_add(&devA.list, &head);
    dump("加 A", &head);

    list_add(&devB.list, &head);
    dump("加 B", &head);

    list_add(&devC.list, &head);
    dump("加 C", &head);
    printf("  注意：每次都插在 head 后面，所以顺序是反的（后进先出）\n");

    printf("\n===== ③ 删中间的 B（不需要找前一个）=====\n");
    list_del(&devB.list);
    dump("删 B", &head);

    printf("\n===== ④ container_of 验算 =====\n");
    {
        /* devC.list 的地址，和 devC 本身的地址 */
        char *pl = (char *)&devC.list;
        char *pc = (char *)&devC;
        printf("  &devC      = %p\n", (void *)pc);
        printf("  &devC.list = %p\n", (void *)pl);
        printf("  差值       = %ld 字节  （应该等于 offsetof）\n",
               (long)(pl - pc));
        printf("  container_of 反算回来 = %p  %s\n",
               (void *)list_entry(&devC.list, struct device, list),
               (list_entry(&devC.list, struct device, list) == &devC) ? "✅ 一致" : "❌ 不一致");
    }

    printf("\n===== ⑤ 反序再遍历一遍（双向链表可以）=====\n");
    {
        struct list_head *pos;
        printf("  逆序: [ ");
        /* 从 head->prev 出发，用 prev 往回走 —— 单链表做不到这件事 */
        for (pos = head.prev; pos != &head; pos = pos->prev)
        {
            struct device *d = list_entry(pos, struct device, list);
            printf("%d:%s ", d->id, d->name);
        }
        printf("]\n");
    }

    return 0;
}
