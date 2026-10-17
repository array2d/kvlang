#include "runtime_internal.h"

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

static void expect(bool ok, const char *msg) {
    if (!ok) fail(msg);
}

int main(void) {
    const char *pc = "/vthread/7/[1]/[3,0]";
    kvlangStrbuf_t out; kvlangStrbufInit(&out);
    expect(kvlangRwirNextPc(pc, &out) == 0, "nextpc rc");
    expect(strcmp(out.p, "/vthread/7/[1]/[4,0]") == 0, "nextpc value");
    kvlangStrbufFree(&out);

    char *fr = kvlangKeytreeFrameRoot(pc);
    expect(fr && strcmp(fr, "/vthread/7/[1]") == 0, "frameroot");
    expect(kvlangKeytreeFrameDepth(pc) == 1, "depth from pc");
    expect(kvlangKeytreeFrameDepth(fr) == 1, "depth from frame");
    expect(kvlangKeytreeFrameDepth("/vthread/7/[3]") == 3, "depth 3 frame");
    expect(kvlangKeytreeFrameDepth("/vthread/7/[3]/[5,0]") == 3, "depth 3 from nested pc");
    free(fr);

    char *at = kvlangKeytreeFrameAt("7", 3);
    expect(at && strcmp(at, "/vthread/7/[3]") == 0, "frameat");
    free(at);

    kvlangStrbuf_t vtid; kvlangStrbufInit(&vtid);
    int d = 0, ir = 0, prm = 0;
    kvlangKeytreeParsePc("/vthread/42/[2]/[9,-1]", &vtid, &d, &ir, &prm);
    expect(strcmp(vtid.p, "42") == 0, "parse vtid");
    expect(d == 2 && ir == 9 && prm == -1, "parse coords");
    kvlangStrbufFree(&vtid);

    char *ep = kvlangKeytreeEntryPc("/vthread/7/[1]");
    expect(ep && strcmp(ep, "/vthread/7/[1]/[1,0]") == 0, "entrypc");
    free(ep);

    printf("ok\n");
    return 0;
}
