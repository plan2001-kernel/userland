#include "core.h"
static unsigned char large[GB_LARGE+128],small[GB_SMALL+128],input[GB_LARGE+128];
static char output[8192];static U32 used;
static const unsigned char meta[]="\xa1\x67profile\x74plan2001.geobench.v1";
static const unsigned char resultmeta[]="\xa2\x67profile\x74plan2001.geobench.v1\x64name\x75geobench-plan2001.csv";
static struct Result {const char *phase;U32 ops,bytes,stored,completions;U64 ns;} rows[GB_ROUNDS*GB_PHASES];
static U32 row_count;
void *memset(void *p,int value,unsigned long n){unsigned char *q=p;while(n--)*q++=(unsigned char)value;return p;}
void *memcpy(void *p,const void *source,unsigned long n){unsigned char*q=p;const unsigned char*s=source;while(n--)*q++=*s++;return p;}
int gb_equal(const void*a,const void*b,U32 n){const unsigned char*p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static void be(unsigned char *p,U64 x,U32 n){while(n--){p[n]=(unsigned char)x;x>>=8;}}
U32 gb_frame(unsigned char *p,U32 raw,int result)
{
    const unsigned char*m=result?resultmeta:meta;U32 n=result?sizeof(resultmeta)-1:sizeof(meta)-1;
    memcpy(p+raw,m,n);unsigned char*f=p+raw+n;
    memcpy(f,"P2OTAIL1",8);be(f+8,1,4);be(f+12,0,4);be(f+16,raw,8);be(f+24,n,8);return raw+n+32;
}
static void text(const char*s){while(*s && used<sizeof(output)-1)output[used++]=*s++;}
static void number(U64 n){char a[24];U32 count=0;do{a[count++]=(char)('0'+n%10);n/=10;}while(n);while(count && used<sizeof(output)-1)output[used++]=a[--count];}
static int elapsed(U64 start,U64 *out){U64 end=gb_now();if(!start || end<=start)return -1;*out=end-start;return 0;}
static int row(const char*phase,U32 ops,U32 bytes,U32 stored,U32 calls,U64 start)
{
    struct Result*r=&rows[row_count++];r->phase=phase;r->ops=ops;r->bytes=bytes;r->stored=stored;r->completions=calls;return elapsed(start,&r->ns);
}
static int read_phase(const char *phase,GBObject *objects,U32 count,U32 passes,unsigned char *expected,U32 raw,U32 stored)
{
    U64 total=0,start,delta;U32 i,j;
    for(j=0;j<passes;j++)for(i=0;i<count;i++){
        start=gb_now();long n=gb_read(&objects[i],input,stored);
        if(elapsed(start,&delta))return -1;
        total+=delta;
        /* All bytes checked AFTER stopping this read's timer. */
        if(n!=(long)stored || !gb_equal(input,expected,stored))return -1;
    }
    struct Result*r=&rows[row_count++];r->phase=phase;r->ops=count*passes;r->bytes=raw*count*passes;r->stored=stored*count*passes;r->completions=0;r->ns=total;return 0;
}
static int check(GBObject *o,const unsigned char *expected,U32 n){return gb_read(o,input,n)!=(long)n || !gb_equal(input,expected,n)?-1:0;}
static int round_once(int smoke)
{
    GBObject big={-1,0},objects[GB_COUNT];U32 count=smoke?2:GB_COUNT,passes=smoke?1:GB_PASSES;
    U32 raw=smoke?65536:GB_LARGE,ln=gb_frame(large,raw,0),sn=gb_frame(small,GB_SMALL,0),i;U64 start;
    for(i=0;i<GB_COUNT;i++)objects[i].handle=-1;
    start=gb_now();if(gb_create(&big,large,ln) || row("large_create",1,raw,ln,gb_create_completions(),start))goto bad;
    if(check(&big,large,ln))goto bad;
    start=gb_now();
    for(i=0;i<passes;i++){
        large[0]^=0x5a;
        if(gb_put(&big,large,ln) || gb_commit())goto bad;
    }
    if(row("large_replace_commit",passes,raw*passes,ln*passes,passes*gb_commit_completions(1),start) || check(&big,large,ln))goto bad;
    if(read_phase("large_read_warm",&big,1,passes,large,raw,ln))goto bad;
    start=gb_now();
    for(i=0;i<count;i++)if(gb_create(&objects[i],small,sn))goto bad;
    if(row("small_create_commit",count,GB_SMALL*count,sn*count,count*gb_create_completions(),start))goto bad;
    if(read_phase("small_read_warm",objects,count,1,small,GB_SMALL,sn))goto bad;
    small[0]^=0x5a;start=gb_now();
    for(i=0;i<count;i++)if(gb_put(&objects[i],small,sn) || gb_commit())goto bad;
    if(row("small_replace_commit",count,GB_SMALL*count,sn*count,count*gb_commit_completions(1),start))goto bad;
    for(i=0;i<count;i++)if(check(&objects[i],small,sn))goto bad;
    small[0]^=0x5a;start=gb_now();
    for(i=0;i<count;i++)if(gb_put(&objects[i],small,sn))goto bad;
    if(gb_commit() || row("small_replace_batch",count,GB_SMALL*count,sn*count,gb_commit_completions(count),start))goto bad;
    for(i=0;i<count;i++)if(check(&objects[i],small,sn))goto bad;
    for(i=0;i<count;i++)if(gb_close(&objects[i]))goto bad;
    return gb_close(&big);
bad:
    for(i=0;i<count;i++)if(objects[i].handle>=0)gb_close(&objects[i]);
    if(big.handle>=0)gb_close(&big);
    return -1;
}
int gb_run(int smoke)
{
    U32 x=0x91374621u,i,r,rounds=smoke?1:GB_ROUNDS;
    for(i=0;i<GB_LARGE;i++){x^=x<<13;x^=x>>17;x^=x<<5;large[i]=(unsigned char)x;}
    memcpy(small,large,GB_SMALL);
    for(r=0;r<rounds;r++)if(round_once(smoke))return -1;
    text("geobench_v1,platform,policy,kind,round,phase,operations,payload_bytes,stored_bytes,completion_calls,elapsed_ns\n");
    for(i=0;i<row_count;i++){
        struct Result*v=&rows[i];text("geobench_v1,");text(gb_platform);text(",");text(gb_policy);text(smoke?",smoke,":",benchmark,");number(i/GB_PHASES+1);text(",");text(v->phase);text(",");number(v->ops);text(",");number(v->bytes);text(",");number(v->stored);text(",");number(v->completions);text(",");number(v->ns);text("\n");
    }
    text("# PASS direct whole-object workload; rounds=");number(rounds);text("; all payloads and envelopes verified outside read timers\n");
    return gb_output(output,used);
}
