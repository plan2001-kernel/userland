/* Native F5 external ls launcher, opt-in only.
 * A separate child sees an immutable listing snapshot (RO region), an empty
 * output frame (RW region) and NOTHING else. Execution uses a volume Handle
 * with Vexecute, NOT the Unix command's pathname.
 *
 * The current child is a Plan2001-specific listing applet, not BusyBox's
 * upstream ls.c. Only the caller's PVD grants names. This is intentionally
 * not a general POSIX exec or fork.
 */
#include "uxexternal.h"
#include "uxspawn.h"
#include "obj.h"
#include "vobj.h"

#define EXTERNAL_FRAME 4096u
#define EXTERNAL_DATA (EXTERNAL_FRAME - 8u)
#define EXTERNAL_TIMEOUT_MS 30000LL

extern long obj_region_create(unsigned long long size, ObjId *out);
extern long obj_region_map(int handle, unsigned long long addr);
extern long obj_region_map_flags(int handle, unsigned long long addr,
                                 unsigned int flags);
extern long obj_region_unmap(unsigned long long base);
extern long obj_close(int handle);
extern long obj_process_terminate(int handle);

typedef struct {
    unsigned char *data;
    unsigned int used;
} Listing;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int emit_listing(void *ctx, const void *name, unsigned int n)
{
    Listing *list = (Listing *)ctx;
    const unsigned char *src = (const unsigned char *)name;
    unsigned int i;
    if (!n || n > PVD_LABEL_MAX || n+1u > EXTERNAL_DATA-list->used)
        return -1;
    for (i=0; i<n; ++i) {
        if (src[i] < 32 || src[i] >= 127 || src[i] == '/')
            return -1;
        list->data[list->used+i] = src[i];
    }
    list->used += n;
    list->data[list->used++] = '\n';
    return 0;
}
int uxexternal_ls(void *ctx, const char *command, char *const *argv,
                  unsigned int argc, void *output, unsigned int capacity,
                  unsigned int *size)
{
    UxExternal *runtime = (UxExternal *)ctx;
    Listing names;
    UxLaunch launch = {0};
    char *child_argv[3] = {"busybox", "ls", "/"};
    int source = -1, stdout_region = -1, process = -1, completed = 0;
    unsigned long long source_map = 0, stdout_map = 0, n;
    unsigned int i;
    int status = -1;
    if (!runtime || !runtime->fs || runtime->executable_volume <= 0 ||
        !runtime->executable_oid || !argv || !command || !output ||
        !size || !capacity || argc < 1 || argc > 2 ||
        !equal(command,"ls") || !equal(argv[0],"ls"))
        return -1;
    source = (int)obj_region_create(EXTERNAL_FRAME, 0);
    if (source <= 0) goto cleanup;
    stdout_region = (int)obj_region_create(EXTERNAL_FRAME, 0);
    if (stdout_region <= 0) goto cleanup;
    {
        long mapped = obj_region_map(source, 0);
        if (mapped < 0) goto cleanup;
        source_map = (unsigned long long)mapped;
    }
    {
        long mapped = obj_region_map_flags(stdout_region, 0,
                                           ObjRegionMapReadonly);
        if (mapped < 0) goto cleanup;
        stdout_map = (unsigned long long)mapped;
    }
    names.data = (unsigned char *)(source_map + 8);
    names.used = 0;
    if (uxfs_list(runtime->fs, argc == 2 ? argv[1] : ".",
                  emit_listing, &names) != PVD_OK)
        goto cleanup;
    /* The 64-bit length is an acquire/release publication word. The
     * contents are immutable after this store for the child lifetime.
     */
    __atomic_store_n((unsigned long long *)source_map,
                     (unsigned long long)names.used, __ATOMIC_RELEASE);
    launch.executable_volume = runtime->executable_volume;
    launch.oid = runtime->executable_oid;
    launch.grant_count = 2;
    launch.grant[0].handle = source;
    launch.grant[0].rights = Vread;
    launch.grant[1].handle = stdout_region;
    launch.grant[1].rights = Vwrite;
    process = uxspawn_start(&launch, child_argv, 3);
    if (process <= 0) goto cleanup;
    if (uxspawn_wait(process, EXTERNAL_TIMEOUT_MS, &completed) != 0 ||
        !completed) goto cleanup;
    n = __atomic_load_n((unsigned long long *)stdout_map,
                        __ATOMIC_ACQUIRE);
    if (n > EXTERNAL_DATA || n > capacity) goto cleanup;
    for (i = 0; i < (unsigned int)n; ++i)
        ((unsigned char *)output)[i] =
            ((const unsigned char *)(stdout_map + 8))[i];
    *size = (unsigned int)n;
    status = 0;
cleanup:
    if (process > 0) {
        if (status != 0) (void)obj_process_terminate(process);
        (void)obj_close(process);
    }
    if (stdout_map) (void)obj_region_unmap(stdout_map);
    if (source_map) (void)obj_region_unmap(source_map);
    if (stdout_region > 0) (void)obj_close(stdout_region);
    if (source > 0) (void)obj_close(source);
    return status;
}
