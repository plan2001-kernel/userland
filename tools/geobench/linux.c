#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "core.h"
static int directory;
static int pending[GB_COUNT+1];static U32 count,serial;
const char *gb_platform="linux",*gb_policy="ext4_fsync";
U64 gb_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (U64)t.tv_sec*1000000000ULL+t.tv_nsec;}
static int replace(int fd,const void *p,U32 n){U32 done=0;while(done<n){ssize_t r=pwrite(fd,(const unsigned char*)p+done,n-done,done);if(r<0 && errno==EINTR)continue;if(r<=0)return -1;done+=(U32)r;}return ftruncate(fd,n);}
int gb_create(GBObject *o,const void *p,U32 n){char name[64];snprintf(name,sizeof name,"object-%u",++serial);int fd=open(name,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return -1;o->handle=fd;o->oid=serial;return replace(fd,p,n) || fsync(fd) || fsync(directory)?-1:0;}
int gb_put(GBObject *o,const void *p,U32 n){if(count>=GB_COUNT+1 || replace((int)o->handle,p,n))return -1;pending[count++]=(int)o->handle;return 0;}
int gb_commit(void){for(U32 i=0;i<count;i++)if(fsync(pending[i]))return -1;count=0;return 0;}
long gb_read(GBObject *o,void *p,U32 n){return pread((int)o->handle,p,n,0);}
int gb_close(GBObject *o){int r=close((int)o->handle);o->handle=-1;return r;}
U32 gb_create_completions(void){return 2;}
U32 gb_commit_completions(U32 objects){return objects;}
int gb_output(const void *p,U32 n){return fwrite(p,1,n,stdout)==n && !fflush(stdout)?0:-1;}
int main(int argc,char **argv)
{
    if(argc<2 || argc>3 || (argc==3 && !gb_equal(argv[2],"smoke",6))){fprintf(stderr,"usage: geobench-linux EXT4_DIRECTORY [smoke]\n");return 2;}
    if(mkdir(argv[1],0700) && errno!=EEXIST){perror("mkdir base");return 1;}
    char path[1024];int i;
    for(i=0;i<100;i++){snprintf(path,sizeof path,"%s/geobench-%ld-%d",argv[1],(long)getpid(),i);if(!mkdir(path,0700))break;if(errno!=EEXIST){perror("mkdir run");return 1;}}
    if(i==100 || chdir(path)){perror("fresh run directory");return 1;}
    directory=open(".",O_RDONLY|O_DIRECTORY);if(directory<0){perror("directory open");return 1;}
    int rc=gb_run(argc==3);if(close(directory))rc=-1;
    if(rc){perror("GEOBENCH INVALID");return 1;}
    fprintf(stderr,"retained whole-object fixtures: %s\n",path);return 0;
}
