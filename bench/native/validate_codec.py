#!/usr/bin/env python3
"""Exhaust-check the current volatile PC/status codec on a separate CPU."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--cpu',type=int,default=25)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
from fast import generate
base=Path(__file__).resolve().parent
source,_=generate((base/'worker.c.in').read_text(),'fast-pc',{'functions':{}})
codec=source[source.index('#if __BYTE_ORDER__'):source.index('static int replay(')]
start=source.index('        if (((volatile word_t *)status)')
end=source.index('            return 1;',start)+len('            return 1;')
status_check=source[start:end].replace('return 1;', 'return 0;')
harness=r"""
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define ROOT "/vthread/native"
#define DEPTH_POS (sizeof(ROOT)+1)
#define ROW_POS (DEPTH_POS+6)
"""+codec+r"""
static unsigned oracle(const uint8_t *s, int n) {
    unsigned v=0;
    for(int i=0;i<n;i++) {
        if(s[i]<'0'||s[i]>'9') return UINT32_MAX;
        v=v*10+s[i]-'0';
    }
    return v;
}
static int status_ok(volatile uint8_t *status) {
@STATUS_CHECK@
    return 1;
}
static void must(int ok) { if(!ok) { fputs("codec mismatch\n",stderr); exit(1); } }
int main(void) {
    uint8_t space[64];
    uint64_t checks=0;
    for(unsigned align=0;align<8;align++) {
        uint8_t *pc=space+align;
        for(unsigned row=0;row<10000;row++) {
            char expected[64];
            snprintf(expected,sizeof expected,ROOT "/[007]/[%04u,0]",row);
            memcpy(pc,ROOT "/[000]/[0001,0]",strlen(ROOT "/[000]/[0001,0]"));
            set_pc(pc,7,row);
            must(!memcmp(pc,expected,strlen(expected)) && digits(pc,DEPTH_POS,3)==7 && digits(pc,ROW_POS,4)==row);
            checks++;
        }
        for(unsigned depth=0;depth<256;depth++) {
            char expected[64];
            snprintf(expected,sizeof expected,ROOT "/[%03u]/[1234,0]",depth);
            set_pc(pc,depth,1234);
            must(!memcmp(pc,expected,strlen(expected)) && digits(pc,DEPTH_POS,3)==depth);checks++;
        }
        for(unsigned value=0;value<256;value++) {
            pc[DEPTH_POS+3]=(uint8_t)value;
            set_pc(pc,17,1234);
            must(pc[DEPTH_POS+3]==value);checks++;
        }
        for(unsigned pos=0;pos<7;pos++) for(unsigned value=0;value<256;value++) {
            memcpy(pc,ROOT "/[001]/[1234,0]",strlen(ROOT "/[001]/[1234,0]"));
            pc[pos<3?DEPTH_POS+pos:ROW_POS+pos-3]=(uint8_t)value;
            must(digits(pc,DEPTH_POS,3)==oracle(pc+DEPTH_POS,3) && digits(pc,ROW_POS,4)==oracle(pc+ROW_POS,4));checks++;
        }
        for(unsigned pos=0;pos<7;pos++) for(unsigned value=0;value<256;value++) {
            memcpy(pc,"running",7);pc[pos]=(uint8_t)value;
            must(status_ok(pc)==(!memcmp(pc,"running",7)));checks++;
        }
    }
    uint8_t *pc=space+1;
    for(uint32_t word=0;word<(UINT32_C(1)<<24);word++) {
        memcpy(pc+DEPTH_POS,&word,3);
        must(digits(pc,DEPTH_POS,3)==oracle(pc+DEPTH_POS,3));checks++;
    }
    for(uint32_t n=0;n<65536;n++) {
        uint32_t word=UINT32_C(0x30303030)^((n&15)|((n&240)<<4)|((n&3840)<<8)|((n&61440)<<12));
        memcpy(pc+ROW_POS,&word,4);
        must(digits(pc,ROW_POS,4)==oracle(pc+ROW_POS,4));checks++;
    }
    size_t pagesize=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mapping=mmap(NULL,2*pagesize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    must(mapping!=MAP_FAILED && !mprotect(mapping+pagesize,pagesize,PROT_NONE));
    uint8_t *status=mapping+pagesize-7;
    memcpy(status,"running",7);must(status_ok(status));checks++;
    pc=mapping+pagesize-strlen(ROOT "/[000]/[0001,0]");
    memcpy(pc,ROOT "/[000]/[0001,0]",strlen(ROOT "/[000]/[0001,0]"));
    set_pc(pc,255,9999);must(digits(pc,DEPTH_POS,3)==255 && digits(pc,ROW_POS,4)==9999);checks++;
    must(!munmap(mapping,2*pagesize));
    printf("codec checks=%llu passed\n",(unsigned long long)checks);
}
"""
harness=harness.replace('@STATUS_CHECK@',status_check)
report={'cpu':a.cpu,'scope':'Decimal PC codec and exact seven-byte status','coverage':['all 2^24 depth-byte combinations','all 16^4 relevant row-nibble combinations','all legal depth/row values at eight alignments','all 256 byte replacements at every PC digit/status byte','guard-page exact body boundaries'],'source_sha256':{name:hashlib.sha256((base/name).read_bytes()).hexdigest() for name in ['fast.py','fast.c.in','worker.c.in']},'harness_sha256':hashlib.sha256(harness.encode()).hexdigest(),'runs':[]}
with tempfile.TemporaryDirectory(prefix='native-review-codec-') as td:
    cfile=Path(td)/'codec.c';cfile.write_text(harness)
    for compiler,flags in [('cc',['-O3']),('clang',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        binary=Path(td)/(compiler+'-codec')
        subprocess.run(['taskset','-c',str(a.cpu),compiler,*flags,'-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True,capture_output=True,text=True)
        result=subprocess.run(['taskset','-c',str(a.cpu),str(binary)],check=False,capture_output=True,text=True,timeout=30)
        report['runs'].append({'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],'flags':flags,'stdout':result.stdout.strip(),'stderr':result.stderr,'exit':result.returncode})
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
raise SystemExit(any(run['exit'] for run in report['runs']))
