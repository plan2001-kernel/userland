/* Native ABI only: no Newlib, POSIX, project, fd table or path operations. */
#include "core.h"
#include "obj.h"
#include "vobj.h"
#include "sysctl.h"
extern long objcreate(U64,U64,void*,U64,void*),objopen(U64,void*,long,U64,void*),objclose(int),objinvoke(int,void*,long);
extern U64 _nsec(void);
extern long errstr(char *,long);
extern void _exits(char *);
static int volume,result,console;
static VobjListRec handles[VhandleLimit];
static unsigned char result_data[8320];
const char *gb_platform="plan2001",*gb_policy="direct_ge_commit";
static long invoke(int h,ObjCall *c){c->version=OBJ_ABI_VERSION;return objinvoke(h,c,sizeof *c)<0?-1:(long)c->result;}
U64 gb_now(void){return _nsec();}
int gb_create(GBObject *o,const void *p,U32 n){ObjId id;long h=objcreate(volume,ObjKindGe,(void*)p,n,&id);if(h<0)return -1;o->handle=h;o->oid=id.oid;return 0;}
int gb_put(GBObject *o,const void *p,U32 n){ObjCall c={0};c.op=ObjOpPutOid;c.arg0=o->oid;c.in=(void*)p;c.inlen=n;return invoke(volume,&c)<0?-1:0;}
int gb_commit(void){ObjCall c={0};c.op=ObjOpCommit;return invoke(volume,&c)<0?-1:0;}
long gb_read(GBObject *o,void *p,U32 n){ObjCall c={0};c.op=ObjOpRead;c.out=p;c.outlen=n;return invoke((int)o->handle,&c);}
int gb_close(GBObject *o){int rc=(int)objclose((int)o->handle);o->handle=-1;return rc<0?-1:0;}
U32 gb_create_completions(void){return 1;}
U32 gb_commit_completions(U32 objects){(void)objects;return 1;}
static int console_write(const void *p,U32 n){if(!console)return 0;ObjCall c={0};c.op=ObjOpWrite;c.in=(void*)p;c.inlen=n;return invoke(console,&c)<0?-1:0;}
int gb_output(const void *p,U32 n)
{
    if(n>8192)return -1;
    memcpy(result_data,p,n);U32 size=gb_frame(result_data,n,1);
    ObjCall c={0};c.op=ObjOpWrite;c.in=result_data;c.inlen=size;
    if(invoke(result,&c)<0)return -1;
    memset(&c,0,sizeof c);c.op=ObjOpCommit;
    if(invoke(result,&c)<0)return -1;
    console_write(p,n);return 0;
}
static int init(void)
{
    SysctlReq r={0};r.version=SYSCTL_ABI_VERSION;r.data=handles;r.datalen=sizeof handles;
    if(sysctl(SysctlScopeOwnProcess,SysctlHandleList,&r,sizeof r)<0)return -1;
    if(r.result<0 || r.result>VhandleLimit)return -1;
    U32 need=Vread|Vwrite|Vcreate|Vcommit|Vmeta|Vdelegate;
    for(long i=0;i<r.result;i++){
        VobjListRec*h=&handles[i];
        if(h->kind==VkindGeVolume && (h->rights&need)==need){if(volume)return -1;volume=h->handle;}
        if(h->kind==VkindGe && (h->rights&(Vwrite|Vcommit|Vmeta))==(Vwrite|Vcommit|Vmeta)){if(result)return -1;result=h->handle;}
    }
    ObjOpenConsoleReq output={1,0};long h=objopen(ObjOpenConsole,&output,sizeof output,Vwrite,0);if(h>0)console=(int)h;
    return volume && result?0:-1;
}
_Noreturn void p9_start(int argc,char **argv)
{
    int smoke=argc==2 && gb_equal(argv[1],"smoke",6);
    static const char running[]="GEOBENCH RUNNING OR FAILED: no valid result yet\n";
    if((argc!=1 && !smoke) || init() || gb_output(running,sizeof running-1) || gb_run(smoke)){
        char e[128]={0};errstr(e,sizeof e);
        static const char message[]="GEOBENCH INVALID: native capability or GE/O operation failed\n";
        console_write(message,sizeof message-1);
        U32 n=0;while(n<sizeof e && e[n])n++;console_write(e,n);
        _exits("GEOBENCH INVALID");
    }else _exits("");
    for(;;);
}
