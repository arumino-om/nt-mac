/* CRT の各機能を確認する．失敗すると内容を表示して終了コード 1 で終わる */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void check_format(const char* expected, const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (strcmp(buf, expected) != 0 || n != (int)strlen(expected)) {
        printf("FAIL format \"%s\": expected \"%s\", got \"%s\" (%d)\n", format, expected, buf, n);
        failures++;
    }
}

static void on_exit_handler(void) {
    printf("atexit called\n");
}

int main(int argc, char** argv) {
    /* コマンドライン: features.exe one "two words" three\"quote */
    CHECK(argc == 4);
    if (argc == 4) {
        CHECK(strcmp(argv[1], "one") == 0);
        CHECK(strcmp(argv[2], "two words") == 0);
        CHECK(strcmp(argv[3], "three\"quote") == 0);
    }

    /* 環境変数 (テストで NTMAC_TEST_ENV=hello を設定する) */
    const char* env = getenv("NTMAC_TEST_ENV");
    CHECK(env && strcmp(env, "hello") == 0);

    /* 整数 */
    check_format("42", "%d", 42);
    check_format("-7|+7| 7", "%d|%+d|% d", -7, 7, 7);
    check_format("[   42][42   ][00042]", "[%5d][%-5d][%05d]", 42, 42, 42);
    check_format("ff FF 0xff 377 0377", "%x %X %#x %o %#o", 255, 255, 255, 255, 255);
    check_format("18446744073709551615", "%llu", 18446744073709551615ULL);
    check_format("-9223372036854775808", "%lld", (long long)(-9223372036854775807LL - 1));
    check_format("00123", "%.5d", 123);
    check_format("", "%.0d", 0);
    check_format("4294967295", "%lu", 4294967295UL);
    check_format("65535", "%hu", 65535);
    check_format("123456789012", "%zu", (size_t)123456789012ULL);
    check_format("00000000DEADBEEF", "%p", (void*)0xDEADBEEF);

    /* 文字と文字列 */
    check_format("a|abc|ab|  abc|abc  ", "%c|%s|%.2s|%5s|%-5s", 'a', "abc", "abc", "abc", "abc");
    check_format("(null)", "%s", (char*)NULL);
    check_format("wide", "%ls", L"wide");
    check_format("100%", "%d%%", 100);

    /* 浮動小数点 */
    check_format("3.141593", "%f", 3.14159265);
    check_format("3.142", "%.3f", 3.14159265);
    check_format("-0.500000", "%f", -0.5);
    check_format("1.234568e+04", "%e", 12345.678);
    check_format("1.23E-05", "%.2E", 0.0000123);
    check_format("0.0001", "%g", 0.0001);
    check_format("1e-05", "%g", 0.00001);
    check_format("123457", "%g", 123456.7);
    check_format("1.23457e+06", "%g", 1234567.0);
    check_format("100", "%g", 100.0);
    check_format("0", "%g", 0.0);
    check_format("1", "%.0f", 0.5 + 0.5);
    check_format("10.0", "%.1f", 9.96);
    check_format("0.001", "%.3f", 0.0005);
    check_format("inf -inf", "%f %f", 1.0 / 0.0, -1.0 / 0.0);
    check_format("   1.50", "%7.2f", 1.5);
    check_format("0x1.8000000000000p+1", "%a", 3.0);

    /* snprintf の切り詰め */
    char small[5];
    CHECK(snprintf(small, sizeof(small), "%s", "abcdefgh") == 8);
    CHECK(strcmp(small, "abcd") == 0);

    /* ヒープ */
    char* p = malloc(16);
    CHECK(p != NULL);
    strcpy(p, "heap");
    p = realloc(p, 100000);    /* 大きな確保 (仮想メモリを直接使う) への移動 */
    CHECK(p && strcmp(p, "heap") == 0);
    free(p);
    int* zeros = calloc(1000, sizeof(int));
    int all_zero = 1;
    for (int i = 0; i < 1000; i++) all_zero &= zeros[i] == 0;
    CHECK(all_zero);
    free(zeros);

    atexit(on_exit_handler);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("crt test ok\n");
    return 0;
}
