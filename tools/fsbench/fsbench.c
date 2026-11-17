/* Small, identical POSIX workload for ext4 and native Plan2001 Unix I/O.
 * No crash/recovery, cache eviction, direct I/O, or privilege is involved.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BLOCK 65536u
#define FILE_BYTES (1024u*1024u)
#define SMALL_BYTES 4096u
#define SMALL_COUNT 32u
#define READ_PASSES 8u
#define MAX_ROUNDS 9
#define PHASES 5
static unsigned char pattern[BLOCK],buffer[BLOCK];
static char work[512];
static int durable,dirfd=-1;
static const char *mode;
static struct Result {const char *phase;unsigned ops,bytes;long long us;} results[MAX_ROUNDS*PHASES];
static unsigned result_count;
static void fail(const char *op)
{
    fprintf(stderr,"fsbench INVALID: %s: %s; directory=%s\n",op,strerror(errno),work);
    exit(1);
}
static long long now(void)
{
    struct timeval t;
    if(gettimeofday(&t,NULL))fail("gettimeofday");
    return (long long)t.tv_sec*1000000LL+t.tv_usec;
}
static void finish(const char *phase,unsigned ops,unsigned bytes,long long start)
{
    long long elapsed=now()-start;
    if(elapsed<=0){errno=ERANGE;fail("nonpositive clock interval; do not adjust system clock during test");}
    results[result_count].phase=phase;results[result_count].ops=ops;
    results[result_count].bytes=bytes;results[result_count++].us=elapsed;
}
static void file_sync(int fd)
{
#ifndef __plan2001__
    if(durable && fsync(fd))fail("fsync file");
#else
    (void)fd; /* Native writes already publish a GE/O transaction. */
#endif
}
static void directory_sync(void)
{
#ifndef __plan2001__
    if(durable && fsync(dirfd))fail("fsync directory");
#endif
}
static void checked_close(int fd){if(close(fd))fail("close");}
static int create(const char *name)
{
    int fd=open(name,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)fail("exclusive create");
    file_sync(fd);directory_sync();return fd;
}
static void write_block(int fd,unsigned n)
{
    unsigned used=0;
    while(used<n){
        long got=write(fd,pattern+used,n-used);
        if(got<0 && errno==EINTR)continue;
        if(got<=0){if(!got)errno=EIO;fail("write");}
        used+=(unsigned)got;file_sync(fd);
    }
}
static void read_checked(const char *name,unsigned bytes,unsigned block)
{
    int fd=open(name,O_RDONLY);unsigned off=0;
    if(fd<0)fail("open for read");
    while(off<bytes){
        unsigned n=bytes-off;if(n>block)n=block;
        unsigned used=0;
        while(used<n){
            long got=read(fd,buffer+used,n-used);
            if(got<0 && errno==EINTR)continue;
            if(got<=0){if(!got)errno=EIO;fail("read length");}
            used+=(unsigned)got;
        }
        if(memcmp(pattern,buffer,n)){errno=EIO;fail("content mismatch");}
        off+=n;
    }
    if(read(fd,buffer,1)!=0){errno=EIO;fail("unexpected EOF/length");}
    checked_close(fd);
}
static void small_name(char out[32],unsigned i){snprintf(out,32,"small-%03u.bin",i);}
static void one_round(unsigned bytes,unsigned count,unsigned passes)
{
    char name[32];long long start;unsigned i;int fd;
    start=now();fd=create("sequential.bin");
    for(i=0;i<bytes;i+=BLOCK)write_block(fd,bytes-i<BLOCK?bytes-i:BLOCK);
    checked_close(fd);finish("seq_write",bytes/BLOCK,bytes,start);
    start=now();
    for(i=0;i<passes;i++)read_checked("sequential.bin",bytes,BLOCK);
    finish("seq_read_warm",passes*(bytes/BLOCK),passes*bytes,start);
    start=now();
    for(i=0;i<count;i++){
        small_name(name,i);fd=create(name);write_block(fd,SMALL_BYTES);checked_close(fd);
    }
    finish("small_create_write_close",count,count*SMALL_BYTES,start);
    start=now();
    for(i=0;i<count;i++){small_name(name,i);read_checked(name,SMALL_BYTES,SMALL_BYTES);}
    finish("small_read_warm",count,count*SMALL_BYTES,start);
    start=now();
    for(i=0;i<count;i++){
        small_name(name,i);if(unlink(name))fail("unlink small file");directory_sync();
    }
    finish("small_unlink",count,0,start);
    if(unlink("sequential.bin"))fail("unlink sequential file");
    directory_sync();
}
static void usage(void)
{
    puts("usage: fsbench --dir DIRECTORY [--rounds 1..9] [--mode buffered|durable|native] [--smoke]");
    puts("Default: 3 rounds, 1 MiB sequential/64 KiB blocks, 8 warm read passes, 32 x 4 KiB files.");
    puts("--smoke is a functional check only: 64 KiB, 1 read pass, 2 small files, 1 round.");
}
int main(int argc,char **argv)
{
    const char *base=NULL;int rounds=3,smoke=0;unsigned i,r;struct stat st;
#ifdef __plan2001__
    const char *platform="plan2001";mode="native";
#else
    const char *platform="linux";mode="buffered";
#endif
    for(i=1;i<(unsigned)argc;i++){
        if(!strcmp(argv[i],"--help")){usage();return 0;}
        if(!strcmp(argv[i],"--smoke")){smoke=1;continue;}
        if(i+1>=(unsigned)argc){usage();return 2;}
        if(!strcmp(argv[i],"--dir"))base=argv[++i];
        else if(!strcmp(argv[i],"--mode"))mode=argv[++i];
        else if(!strcmp(argv[i],"--rounds")){
            char *end;long n=strtol(argv[++i],&end,10);
            if(!*argv[i] || *end || n<1 || n>MAX_ROUNDS){usage();return 2;}rounds=(int)n;
        }else{usage();return 2;}
    }
    if(!base || !*base || strlen(base)>400){usage();return 2;}
#ifdef __plan2001__
    if(strcmp(mode,"native")){fputs("Plan2001 uses native publication mode only.\n",stderr);return 2;}
#else
    if(strcmp(mode,"buffered") && strcmp(mode,"durable")){usage();return 2;}
    durable=!strcmp(mode,"durable");
#endif
    if(smoke)rounds=1;
    /* Fixed nonzero pseudorandom pattern; generation is outside all timers. */
    {unsigned x=0x91374621u;for(i=0;i<BLOCK;i++){x^=x<<13;x^=x>>17;x^=x<<5;pattern[i]=(unsigned char)x;}}
    if(mkdir(base,0700) && errno!=EEXIST)fail("mkdir base");
    if(stat(base,&st) || !S_ISDIR(st.st_mode)){errno=ENOTDIR;fail("base is not a directory");}
    /* Each invocation owns a fresh directory. Never truncate existing files. */
    for(i=0;i<100;i++){
        snprintf(work,sizeof work,"%s/run-%ld-%u",base,(long)getpid(),i);
        if(mkdir(work,0700)==0)break;
        if(errno!=EEXIST)fail("mkdir fresh run directory");
    }
    if(i==100){errno=EEXIST;fail("fresh run namespace exhausted");}
    if(chdir(work))fail("chdir work directory");
#ifndef __plan2001__
    if(durable){dirfd=open(".",O_RDONLY|O_DIRECTORY);if(dirfd<0)fail("open directory for sync");}
#endif
    for(r=0;r<(unsigned)rounds;r++)one_round(smoke?BLOCK:FILE_BYTES,smoke?2:SMALL_COUNT,smoke?1:READ_PASSES);
    if(dirfd>=0)checked_close(dirfd);
    puts("fsbench_v1,platform,mode,kind,round,phase,operations,bytes,elapsed_us,MiB_per_s,operations_per_s");
    for(i=0;i<result_count;i++){
        struct Result *v=&results[i];
        double seconds=(double)v->us/1000000.0;
        printf("fsbench_v1,%s,%s,%s,%u,%s,%u,%u,%lld,%.3f,%.3f\n",platform,mode,smoke?"smoke":"benchmark",i/PHASES+1,v->phase,v->ops,v->bytes,v->us,(double)v->bytes/1048576.0/seconds,(double)v->ops/seconds);
    }
    printf("# PASS verified all file contents; rounds=%d; working_directory=%s\n",rounds,work);
    puts("# warm reads; no cold-cache or raw-device throughput claim; do not change system time during run");
    return fflush(stdout)?1:0;
}
