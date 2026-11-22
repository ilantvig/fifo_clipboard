#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* Чистое решение: перехватывать ли это нажатие как вставку из очереди. */
bool Logic_ShouldInterceptPaste(uint32_t vk, bool ctrl, bool shift, bool alt,
                                bool injected, bool fifoMode, size_t queueCount);

typedef enum { CLIP_PROCESS, CLIP_IGNORE_OWN, CLIP_IGNORE_DUP } ClipAction;
ClipAction Logic_ClassifyClipboardUpdate(uint32_t seq, uint32_t lastSeenSeq,
                                         uint32_t ownPasteSeq);

void Logic_FormatIconText(int count, bool fifoMode, wchar_t *buf, size_t cap);

typedef struct { uint32_t vk; bool keyUp; } PasteInput;
/* Возвращает число заполненных событий (0, если буфер мал). */
size_t Logic_BuildPasteInputs(bool ctrlHeld, PasteInput *out, size_t cap);

#endif
