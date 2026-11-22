#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdbool.h>

#include "../fifo_queue.h"
#include "../app_logic.h"
#include "../settings.h"
#include "../clipboard.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { ++g_failed; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ================= Очередь ================= */
static void test_queue_init(void)
{
    FifoQueue q; FQ_Init(&q);
    CHECK(FQ_Count(&q) == 0);
    CHECK(FQ_TotalBytes(&q) == 0);
    CHECK(FQ_IsEmpty(&q));
    CHECK(q.maxItems == FIFO_MAX_ITEMS);
    CHECK(q.dedup == (FIFO_ENABLE_DEDUP != 0));
    CHECK(FQ_PopFront(&q) == NULL);
    CHECK(FQ_PopBack(&q) == NULL);
}

static void test_queue_order(void)
{
    FifoQueue q; FQ_Init(&q);
    CHECK(FQ_Push(&q, L"A") == FQ_PUSH_OK);
    CHECK(FQ_Push(&q, L"B") == FQ_PUSH_OK);
    CHECK(FQ_Push(&q, L"C") == FQ_PUSH_OK);
    CHECK(FQ_Count(&q) == 3);

    wchar_t *t = FQ_PopFront(&q); CHECK(t && wcscmp(t, L"A") == 0); FQ_FreeText(t);
    t = FQ_PopFront(&q);          CHECK(t && wcscmp(t, L"B") == 0); FQ_FreeText(t);
    t = FQ_PopBack(&q);           CHECK(t && wcscmp(t, L"C") == 0); FQ_FreeText(t);
    CHECK(FQ_IsEmpty(&q));
    CHECK(FQ_TotalBytes(&q) == 0);
}

static void test_queue_popback(void)
{
    FifoQueue q; FQ_Init(&q);
    FQ_Push(&q, L"A"); FQ_Push(&q, L"B"); FQ_Push(&q, L"C");
    wchar_t *t = FQ_PopBack(&q); CHECK(t && wcscmp(t, L"C") == 0); FQ_FreeText(t);
    t = FQ_PopBack(&q);          CHECK(t && wcscmp(t, L"B") == 0); FQ_FreeText(t);
    t = FQ_PopBack(&q);          CHECK(t && wcscmp(t, L"A") == 0); FQ_FreeText(t);
    CHECK(FQ_PopBack(&q) == NULL);
    CHECK(q.head == NULL && q.tail == NULL);
}

static void test_queue_invalid_and_empty(void)
{
    FifoQueue q; FQ_Init(&q);
    CHECK(FQ_Push(&q, NULL) == FQ_PUSH_INVALID);
    CHECK(FQ_Push(NULL, L"x") == FQ_PUSH_INVALID);
    CHECK(FQ_Push(&q, L"") == FQ_PUSH_OK);      /* пустая строка допустима */
    CHECK(FQ_Count(&q) == 1);
    CHECK(FQ_TotalBytes(&q) == sizeof(wchar_t));
    wchar_t *t = FQ_PopFront(&q); CHECK(t && t[0] == L'\0'); FQ_FreeText(t);
    FQ_FreeText(NULL); /* не должен падать */
}

static void test_queue_dedup(void)
{
    FifoQueue q; FQ_Init(&q);
    q.dedup = true;
    CHECK(FQ_Push(&q, L"A") == FQ_PUSH_OK);
    CHECK(FQ_Push(&q, L"A") == FQ_PUSH_DUPLICATE);
    CHECK(FQ_Count(&q) == 1);
    CHECK(FQ_Push(&q, L"B") == FQ_PUSH_OK);
    CHECK(FQ_Push(&q, L"A") == FQ_PUSH_OK); /* не подряд -> разрешено */
    CHECK(FQ_Count(&q) == 3);

    q.dedup = false;
    CHECK(FQ_Push(&q, L"A") == FQ_PUSH_OK);
    CHECK(FQ_Count(&q) == 4);
    FQ_Clear(&q);
}

static void test_queue_max_items(void)
{
    FifoQueue q; FQ_Init(&q);
    q.maxItems = 3;
    FQ_Push(&q, L"A"); FQ_Push(&q, L"B"); FQ_Push(&q, L"C"); FQ_Push(&q, L"D");
    CHECK(FQ_Count(&q) == 3);
    wchar_t *t = FQ_PopFront(&q); CHECK(t && wcscmp(t, L"B") == 0); FQ_FreeText(t); /* A вытеснен */
    FQ_Clear(&q);
}

static void test_queue_max_item_bytes(void)
{
    FifoQueue q; FQ_Init(&q);
    q.maxItemBytes = 2 * sizeof(wchar_t); /* ровно "a" + NUL */
    CHECK(FQ_Push(&q, L"ab") == FQ_PUSH_TOO_LARGE);
    CHECK(FQ_Push(&q, L"a") == FQ_PUSH_OK);
    FQ_Clear(&q);
}

static void test_queue_max_total_bytes(void)
{
    FifoQueue q; FQ_Init(&q);
    q.maxTotalBytes = 4 * sizeof(wchar_t); /* два элемента по "x" */
    CHECK(FQ_Push(&q, L"a") == FQ_PUSH_OK);
    CHECK(FQ_Push(&q, L"b") == FQ_PUSH_OK);
    CHECK(FQ_TotalBytes(&q) == 4 * sizeof(wchar_t));
    CHECK(FQ_Push(&q, L"c") == FQ_PUSH_OK); /* вытесняет "a" */
    CHECK(FQ_Count(&q) == 2);
    CHECK(FQ_TotalBytes(&q) <= q.maxTotalBytes);
    wchar_t *t = FQ_PopFront(&q); CHECK(t && wcscmp(t, L"b") == 0); FQ_FreeText(t);

    CHECK(FQ_Push(&q, L"toolong") == FQ_PUSH_TOO_LARGE); /* больше общего лимита */
    FQ_Clear(&q);
}

static void test_queue_restore_front(void)
{
    FifoQueue q; FQ_Init(&q);
    FQ_Push(&q, L"A"); FQ_Push(&q, L"B");
    wchar_t *t = FQ_PopFront(&q);
    CHECK(FQ_PushFrontOwned(&q, t) == true); /* владение передано */
    CHECK(FQ_Count(&q) == 2);
    t = FQ_PopFront(&q); CHECK(t && wcscmp(t, L"A") == 0); FQ_FreeText(t);
    t = FQ_PopFront(&q); CHECK(t && wcscmp(t, L"B") == 0); FQ_FreeText(t);
}

static void test_queue_clear(void)
{
    FifoQueue q; FQ_Init(&q);
    FQ_Push(&q, L"A"); FQ_Push(&q, L"B"); FQ_Push(&q, L"C");
    FQ_Clear(&q);
    CHECK(FQ_Count(&q) == 0);
    CHECK(FQ_TotalBytes(&q) == 0);
    CHECK(q.head == NULL && q.tail == NULL);
    CHECK(FQ_PopFront(&q) == NULL);
}

static void test_queue_stress(void)
{
    FifoQueue q; FQ_Init(&q);
    wchar_t buf[32];
    for (int i = 0; i < 200; ++i) {
        StringCchPrintfW(buf, 32, L"item%03d", i);
        CHECK(FQ_Push(&q, buf) == FQ_PUSH_OK);
    }
    CHECK(FQ_Count(&q) == FIFO_MAX_ITEMS);
    wchar_t *t = FQ_PopFront(&q);
    CHECK(t && wcscmp(t, L"item136") == 0); /* 200 - 64 */
    FQ_FreeText(t);
    FQ_Clear(&q);
}

/* ================= Логика ================= */
static void test_intercept_truth_table(void)
{
    for (int ctrl = 0; ctrl < 2; ++ctrl)
    for (int shift = 0; shift < 2; ++shift)
    for (int alt = 0; alt < 2; ++alt)
    for (int inj = 0; inj < 2; ++inj)
    for (int fifo = 0; fifo < 2; ++fifo)
    for (int cnt = 0; cnt < 2; ++cnt)
    for (int vk = 0; vk < 2; ++vk) {
        uint32_t key = vk ? 0x56u : 0x58u; /* V или X */
        bool expected = fifo && cnt && ctrl && !shift && !alt && !inj && (key == 0x56u);
        bool got = Logic_ShouldInterceptPaste(key, ctrl, shift, alt, inj, fifo, (size_t)cnt);
        CHECK(got == expected);
    }
}

static void test_classify(void)
{
    CHECK(Logic_ClassifyClipboardUpdate(5, 5, 0) == CLIP_IGNORE_DUP);
    CHECK(Logic_ClassifyClipboardUpdate(5, 4, 5) == CLIP_IGNORE_OWN);
    CHECK(Logic_ClassifyClipboardUpdate(6, 5, 5) == CLIP_PROCESS);
    CHECK(Logic_ClassifyClipboardUpdate(0, 0, 0) == CLIP_IGNORE_DUP);
}

static void test_format_icon(void)
{
    wchar_t b[16];
    Logic_FormatIconText(0, false, b, 16); CHECK(wcscmp(b, L"-1") == 0);
    Logic_FormatIconText(0, true,  b, 16); CHECK(wcscmp(b, L"0") == 0);
    Logic_FormatIconText(64, true, b, 16); CHECK(wcscmp(b, L"64") == 0);
    Logic_FormatIconText(123, true, b, 16); CHECK(wcscmp(b, L"123") == 0);
    Logic_FormatIconText(64, true, b, 2);  CHECK(wcscmp(b, L"6") == 0); /* безопасное усечение */
    Logic_FormatIconText(1, true, NULL, 0); /* не падает */
}

static void test_build_inputs(void)
{
    PasteInput p[4];
    size_t n = Logic_BuildPasteInputs(true, p, 4);
    CHECK(n == 2);
    CHECK(p[0].vk == 0x56 && !p[0].keyUp);
    CHECK(p[1].vk == 0x56 &&  p[1].keyUp);

    n = Logic_BuildPasteInputs(false, p, 4);
    CHECK(n == 4);
    CHECK(p[0].vk == 0x11 && !p[0].keyUp);
    CHECK(p[1].vk == 0x56 && !p[1].keyUp);
    CHECK(p[2].vk == 0x56 &&  p[2].keyUp);
    CHECK(p[3].vk == 0x11 &&  p[3].keyUp);

    CHECK(Logic_BuildPasteInputs(false, p, 1) == 0);
    CHECK(Logic_BuildPasteInputs(true, NULL, 4) == 0);
}

/* ================= Настройки ================= */
static const wchar_t *TEST_INI = L".\\fq_test_settings.ini";

static void test_settings(void)
{
    DeleteFileW(TEST_INI);
    AppSettings s;
    Settings_Defaults(&s);
    CHECK(!s.fifoMode && !s.sounds);

    CHECK(Settings_Load(TEST_INI, &s) == true); /* файла нет -> defaults */
    CHECK(!s.fifoMode && !s.sounds);

    bool combos[4][2] = { {false,false},{true,false},{false,true},{true,true} };
    for (int i = 0; i < 4; ++i) {
        AppSettings w; w.fifoMode = combos[i][0]; w.sounds = combos[i][1];
        CHECK(Settings_Save(TEST_INI, &w) == true);
        AppSettings r;
        CHECK(Settings_Load(TEST_INI, &r) == true);
        CHECK(r.fifoMode == w.fifoMode);
        CHECK(r.sounds == w.sounds);
    }

    WritePrivateProfileStringW(L"General", L"FifoMode", L"abc", TEST_INI);
    AppSettings r;
    CHECK(Settings_Load(TEST_INI, &r) == true);
    CHECK(!r.fifoMode); /* нечисловое значение -> default */

    CHECK(Settings_Save(NULL, &s) == false);
    CHECK(Settings_Load(TEST_INI, NULL) == false);
    DeleteFileW(TEST_INI);
}

/* ================= Интеграция (опционально) ================= */
#ifdef INTEGRATION_TESTS
static void test_clipboard_roundtrip(void)
{
    DWORD before = GetClipboardSequenceNumber();
    const wchar_t *msg = L"fq_integration_test_текст";
    CHECK(Clipboard_SetUnicodeText(NULL, msg) == true);
    DWORD after = GetClipboardSequenceNumber();
    CHECK(after != before);
    wchar_t *got = Clipboard_CopyUnicodeText(NULL);
    CHECK(got && wcscmp(got, msg) == 0);
    FQ_FreeText(got);
    CHECK(Logic_ClassifyClipboardUpdate(after, before, after) == CLIP_IGNORE_OWN);
}
#endif

int main(void)
{
    test_queue_init();
    test_queue_order();
    test_queue_popback();
    test_queue_invalid_and_empty();
    test_queue_dedup();
    test_queue_max_items();
    test_queue_max_item_bytes();
    test_queue_max_total_bytes();
    test_queue_restore_front();
    test_queue_clear();
    test_queue_stress();

    test_intercept_truth_table();
    test_classify();
    test_format_icon();
    test_build_inputs();

    test_settings();

#ifdef INTEGRATION_TESTS
    test_clipboard_roundtrip();
#endif

    printf("\nChecks: %d, Failed: %d\n", g_checks, g_failed);
    if (g_failed == 0) printf("ALL TESTS PASSED\n");
    return g_failed ? 1 : 0;
}