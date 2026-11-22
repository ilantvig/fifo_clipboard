#include "fifo_queue.h"
#include <stdlib.h>
#include <string.h>

/* Не оптимизируется компилятором в отличие от memset перед free. */
static void fq_secure_zero(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static size_t fq_bytes(size_t chars) { return chars * sizeof(wchar_t); }

void FQ_Init(FifoQueue *q)
{
    if (!q) return;
    q->head = q->tail = NULL;
    q->count = q->totalBytes = 0;
    q->maxItems      = FIFO_MAX_ITEMS;
    q->maxItemBytes  = FIFO_MAX_ITEM_BYTES;
    q->maxTotalBytes = FIFO_MAX_TOTAL_BYTES;
    q->dedup         = (FIFO_ENABLE_DEDUP != 0);
}

void FQ_FreeText(wchar_t *text)
{
    if (!text) return;
    fq_secure_zero(text, fq_bytes(wcslen(text) + 1));
    free(text);
}

wchar_t *FQ_PopFront(FifoQueue *q)
{
    if (!q || !q->head) return NULL;
    FqNode *n = q->head;
    wchar_t *t = n->text;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    q->totalBytes -= fq_bytes(n->chars);
    free(n);
    return t;
}

wchar_t *FQ_PopBack(FifoQueue *q)
{
    if (!q || !q->tail) return NULL;
    FqNode *n = q->tail;
    wchar_t *t = n->text;
    if (q->head == q->tail) {
        q->head = q->tail = NULL;
    } else {
        FqNode *p = q->head;
        while (p->next != q->tail) p = p->next;
        p->next = NULL;
        q->tail = p;
    }
    q->count--;
    q->totalBytes -= fq_bytes(n->chars);
    free(n);
    return t;
}

static void fq_evict_front(FifoQueue *q)
{
    FQ_FreeText(FQ_PopFront(q));
}

FqPushResult FQ_Push(FifoQueue *q, const wchar_t *text)
{
    if (!q || !text) return FQ_PUSH_INVALID;

    size_t chars = wcslen(text) + 1;
    size_t bytes = fq_bytes(chars);
    if (bytes > q->maxItemBytes || bytes > q->maxTotalBytes)
        return FQ_PUSH_TOO_LARGE;

    if (q->dedup && q->tail && wcscmp(q->tail->text, text) == 0)
        return FQ_PUSH_DUPLICATE;

    while (q->count >= q->maxItems && q->head) fq_evict_front(q);
    while (q->totalBytes + bytes > q->maxTotalBytes && q->head) fq_evict_front(q);

    FqNode *n = (FqNode *)malloc(sizeof(FqNode));
    if (!n) return FQ_PUSH_NOMEM;
    n->text = (wchar_t *)malloc(bytes);
    if (!n->text) { free(n); return FQ_PUSH_NOMEM; }
    memcpy(n->text, text, bytes);
    n->chars = chars;
    n->next = NULL;

    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    q->count++;
    q->totalBytes += bytes;
    return FQ_PUSH_OK;
}

bool FQ_PushFrontOwned(FifoQueue *q, wchar_t *text)
{
    if (!q || !text) { FQ_FreeText(text); return false; }
    size_t chars = wcslen(text) + 1;
    FqNode *n = (FqNode *)malloc(sizeof(FqNode));
    if (!n) { FQ_FreeText(text); return false; } /* владение принято */
    n->text = text;
    n->chars = chars;
    n->next = q->head;
    q->head = n;
    if (!q->tail) q->tail = n;
    q->count++;
    q->totalBytes += fq_bytes(chars);
    return true;
}

void FQ_Clear(FifoQueue *q)
{
    if (!q) return;
    while (q->head) fq_evict_front(q);
    q->head = q->tail = NULL;
    q->count = q->totalBytes = 0;
}

size_t FQ_Count(const FifoQueue *q)      { return q ? q->count : 0; }
size_t FQ_TotalBytes(const FifoQueue *q) { return q ? q->totalBytes : 0; }
bool   FQ_IsEmpty(const FifoQueue *q)    { return FQ_Count(q) == 0; }
