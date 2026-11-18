#define KVLANG_XVALUE_IMPLEMENTATION
#include <kvlangxvalue/kvlangxvalue.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    kvlangXvalueTextFailure failure = KVLANG_XVALUE_TEXT_OK;
    size_t body_length = 0;
    if (!kvlangXvalueTextToBytes("int64", 5, "42", 2, NULL, 0, &body_length,
                                 &failure) ||
        body_length != 8) {
        return 1;
    }
    uint8_t body[8];
    size_t written = 0;
    if (!kvlangXvalueTextToBytes("int64", 5, "42", 2, body, sizeof body,
                                 &written, &failure) ||
        written != 8) {
        return 1;
    }
    char text[16];
    size_t text_length = 0;
    if (!kvlangXvalueTextFromBytes("int64", 5, body, written, text,
                                   sizeof text, &text_length, &failure) ||
        text_length != 2 ||
        strcmp(text, "42") != 0) {
        return 1;
    }
    if (kvlangConstLanguage("KVLANG_KIND_MAP") == NULL) {
        return 1;
    }
    printf("header_only: ok\n");
    return 0;
}
