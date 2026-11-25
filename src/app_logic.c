#include "app_logic.h"

#include <windows.h>  /* VK_CONTROL */

bool Logic_ShouldInterceptPaste(uint32_t vk, bool ctrl, bool shift, bool alt,
                                bool injected, bool fifoMode, size_t queueCount)
{
    return fifoMode && queueCount > 0 &&
           ctrl && !shift && !alt && !injected && vk == 'V';
}

ClipAction Logic_ClassifyClipboardUpdate(uint32_t seq, uint32_t lastSeenSeq,
                                         uint32_t ownPasteSeq)
{
    if (seq == lastSeenSeq)  return CLIP_IGNORE_DUP;
    if (ownPasteSeq != 0 && (int32_t)(seq - ownPasteSeq) <= 0) return CLIP_IGNORE_OWN;
    return CLIP_PROCESS;
}

void Logic_FormatIconText(int count, bool fifoMode, wchar_t *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    buf[0] = L'\0';

    if (!fifoMode) {
        if (cap >= 3) { buf[0] = L'-'; buf[1] = L'1'; buf[2] = L'\0'; }
        else if (cap >= 2) { buf[0] = L'-'; buf[1] = L'\0'; }
        return;
    }
    if (count < 0) count = 0;

    wchar_t tmp[16];
    int n = 0;
    if (count == 0) {
        tmp[n++] = L'0';
    } else {
        wchar_t rev[16];
        int r = 0;
        while (count > 0 && r < 16) { rev[r++] = (wchar_t)(L'0' + count % 10); count /= 10; }
        while (r > 0) tmp[n++] = rev[--r];
    }
    tmp[n] = L'\0';

    size_t i = 0;
    for (; i + 1 < cap && tmp[i]; ++i) buf[i] = tmp[i];
    buf[i] = L'\0';
}

size_t Logic_BuildPasteInputs(bool ctrlHeld, PasteInput *out, size_t cap)
{
    size_t need = ctrlHeld ? 2u : 4u;
    if (!out || cap < need) return 0;
    size_t i = 0;
    if (!ctrlHeld) { out[i].vk = VK_CONTROL; out[i].keyUp = false; ++i; }
    out[i].vk = 'V'; out[i].keyUp = false; ++i;
    out[i].vk = 'V'; out[i].keyUp = true;  ++i;
    if (!ctrlHeld) { out[i].vk = VK_CONTROL; out[i].keyUp = true; ++i; }
    return i;
}