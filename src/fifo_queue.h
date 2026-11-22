#ifndef FIFO_QUEUE_H
#define FIFO_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

/* Настраиваемые политики очереди. */
#define FIFO_ENABLE_DEDUP      1u                  /* 1 = пропускать повтор последнего элемента */
#define FIFO_MAX_ITEMS         64u                 /* максимум элементов */
#define FIFO_MAX_ITEM_BYTES    (2u * 1024u * 1024u)  /* максимум на один элемент (2 МиБ) */
#define FIFO_MAX_TOTAL_BYTES   (16u * 1024u * 1024u) /* суммарный лимит (16 МиБ) */

typedef struct FqNode {
    wchar_t       *text;   /* владеющий указатель, NUL-terminated */
    size_t         chars;  /* длина включая NUL */
    struct FqNode *next;
} FqNode;

typedef struct {
    FqNode *head;
    FqNode *tail;
    size_t  count;
    size_t  totalBytes;
    size_t  maxItems;
    size_t  maxItemBytes;
    size_t  maxTotalBytes;
    bool    dedup;
} FifoQueue;

typedef enum {
    FQ_PUSH_OK,
    FQ_PUSH_DUPLICATE,
    FQ_PUSH_TOO_LARGE,
    FQ_PUSH_NOMEM,
    FQ_PUSH_INVALID
} FqPushResult;

void         FQ_Init(FifoQueue *q);
FqPushResult FQ_Push(FifoQueue *q, const wchar_t *text);
wchar_t     *FQ_PopFront(FifoQueue *q);          /* старый элемент; caller владеет text */
wchar_t     *FQ_PopBack(FifoQueue *q);           /* свежий элемент; caller владеет text */
bool         FQ_PushFrontOwned(FifoQueue *q, wchar_t *text); /* забирает владение всегда */
void         FQ_Clear(FifoQueue *q);
size_t       FQ_Count(const FifoQueue *q);
size_t       FQ_TotalBytes(const FifoQueue *q);
bool         FQ_IsEmpty(const FifoQueue *q);
void         FQ_FreeText(wchar_t *text);         /* безопасное затирание + free */

#endif
