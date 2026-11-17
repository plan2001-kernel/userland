/* Shared whole-object workload. No Unix, pathname, libc or Python dependency. */
typedef unsigned long long U64;
typedef unsigned int U32;
typedef struct {long handle;U64 oid;} GBObject;
#define GB_LARGE (1024u*1024u)
#define GB_SMALL 4096u
#define GB_COUNT 32u
#define GB_PASSES 8u
#define GB_ROUNDS 3u
#define GB_PHASES 7u
extern const char *gb_platform,*gb_policy;
U64 gb_now(void);
int gb_create(GBObject *,const void *,U32);
int gb_put(GBObject *,const void *,U32);
int gb_commit(void);
long gb_read(GBObject *,void *,U32);
int gb_close(GBObject *);
int gb_output(const void *,U32);
U32 gb_create_completions(void);
U32 gb_commit_completions(U32);
int gb_run(int smoke);
void *memset(void *,int,unsigned long);
void *memcpy(void *,const void *,unsigned long);
int gb_equal(const void *,const void *,U32);
U32 gb_frame(unsigned char *,U32,int results);
