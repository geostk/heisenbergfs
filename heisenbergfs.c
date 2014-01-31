#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include <stdio.h>

#define HEIS_FILE_MAX_SIZE 256
#define HEIS_FILE_NAME_MAX 256
#define HEIS_FILE_MAX_COUNT 256


enum state_code {
    hNew,
    hReaddir,
    hCreate,
    hGetattr,
    hUtime,
    hOpen,
    hTruncate,
    hRead,
    hWrite,
    hRelease,
};

enum ret_code {
    hObserved,
    hMoved,
};

struct transition {
    enum state_code src_state;
    enum state_code dst_state;
    enum ret_code   ret_code;
};

/* touch <file>
 *   create getattr utimens getattr release
 *   getattr open utimens getattr release
 */
/* truncate -s <file>
 *   create getattr truncate getattr release
 *   getattr open   truncate getattr release
 */
/* echo 'hi' > <file>
 *   create getattr write release
 *   getattr open truncate getattr write write release
 */
/* echo 'hi' >> <file>
 *   create getattr write write release
 *   getattr open write write release
 */
/* cat <file>
 *   <N/A>
 *   getattr open read read getattr release
 */
/* ls -lh <file>
 *   <N/A>
 *   getattr
 */

struct transition state_transitions[] = {
    {hNew, hCreate, hObserved},
    {hNew, hGetattr, hObserved},
    {hCreate, hRelease, hMoved},
    {hOpen, hRelease, hMoved},
    {hNew, hGetattr, hObserved},
    {hGetattr, hGetattr, hMoved},
};

typedef struct heisenbergfile
{
    char name[37];
    uuid_t uuid;
    char data[HEIS_FILE_MAX_SIZE];
    mode_t mode;
    uid_t uid;
    gid_t gid;
    struct timespec tv[2];
    off_t size;
    enum state_code observed;
} heisenbergfile_t;

bool observe_file(heisenbergfile_t *file, enum state_code state)
{
    bool retval = false;
    printf("    observe_file: %s (%d -> %d)\n", file->name, file->observed, state);
    file->observed = state;
    return retval;
}

typedef struct heisenbergfs
{
    uid_t uid;
    gid_t gid;
    heisenbergfile_t **files;
    size_t file_count;
    size_t dir_count;
} heisenbergfs_t;

heisenbergfile_t *get_file(heisenbergfs_t *hfs, const char *name)
{
    heisenbergfile_t *file = NULL;
    for(int i = 0; i < hfs->file_count; ++i) {
        if(strncmp(name + 1, hfs->files[i]->name, HEIS_FILE_NAME_MAX) == 0) {
            file = hfs->files[i];
        }
    }
    return file;
}

void *heisenberg_init (struct fuse_conn_info *conn)
{
    heisenbergfs_t *retval = malloc(sizeof *retval);
    memset(retval, 0, sizeof(*retval));
    retval->uid = getuid();
    retval->gid = getgid();
    retval->files = malloc(sizeof(*(retval->files)) * HEIS_FILE_MAX_COUNT);
    return retval;
}
void  heisenberg_destroy (void *context)
{
    free(context);
}
int   heisenberg_readdir (const char *name, void *buf, fuse_fill_dir_t filler, off_t index, struct fuse_file_info *finfo)
{
    printf("  readdir: %s\n", name);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    int retval = 0;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    int i = 0;
    for(i = 0; i < hfs->file_count; ++i)
    {
        heisenbergfile_t *file = hfs->files[i];
        printf("  readdir: %s: observed %d\n", file->name, file->observed);
        (void)observe_file(file, hReaddir);
        filler(buf, file->name, NULL, 0);
    }
    return retval;
}
int   heisenberg_getattr (const char *name, struct stat *stbuf)
{
    int retval = 0;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    //printf("  getattr: %s: %zd\n", name, hfs->file_count);

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(name, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2 + hfs->file_count + hfs->dir_count;
        stbuf->st_uid = hfs->uid;
        stbuf->st_gid = hfs->gid;
    } else {
        retval = -ENOENT;
        heisenbergfile_t *file = get_file(hfs, name);
        if(file) {
            printf("  getattr: %s: observed %d\n", file->name, file->observed);
            (void)observe_file(file, hGetattr);
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_mode = file->mode;
            stbuf->st_uid = file->uid;
            stbuf->st_gid = file->gid;
            stbuf->st_size = file->size;
            retval = 0;
        }
    }
    return retval;
}
int   heisenberg_create (const char *name, mode_t mode, struct fuse_file_info *finfo)
{
    int retval = 0;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    //printf("  create: %s: %zd\n", name, hfs->file_count);

    heisenbergfile_t *file = malloc(sizeof(*file));
    memset(file, 0, sizeof(*file));
    uuid_generate(file->uuid);
    //uuid_unparse(file->uuid, file->name);
    strncpy(file->name, name + 1, 36);
    file->uid = context->uid;
    file->gid = context->gid;
    //file->mode = (S_IFREG | 0666) & context->mask;
    file->mode = mode;
    printf("  create: %s: observed %d\n", file->name, file->observed);
    (void)observe_file(file, hCreate);

    hfs->files[hfs->file_count++] = file;
    //printf("  create: %s: %s: %d\n", name, file->name, retval);
    return retval;
}
int   heisenberg_open (const char *name, struct fuse_file_info *finfo)
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    //printf("  open: %s: %zd\n", name, hfs->file_count);

    heisenbergfile_t *file = get_file(hfs, name);
    if(file) {
        printf("  open: %s: observed %d\n", file->name, file->observed);
        (void)observe_file(file, hOpen);
        retval = 0;//-ENOENT;
    }
    return retval;
}

int   heisenberg_utimens (const char * name, const struct timespec tv[2])
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    //printf("  utimens: %s: %zd\n", name, hfs->file_count);

    heisenbergfile_t *file = get_file(hfs, name);
    if(file) {
        printf("  utimens: %s: observed %d\n", file->name, file->observed);
        (void)observe_file(file, hUtime);
        file->tv[0] = tv[0];
        file->tv[1] = tv[1];
        retval = 0;
    }
    return retval;
}
int   heisenberg_mkdir (const char *name, mode_t mode)
{
    return -EOPNOTSUPP;
}
int   heisenberg_write (const char *name, const char *buf, size_t length, off_t offset, struct fuse_file_info *finfo)
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    printf("  write: %s: %zd\n", name, hfs->file_count);
    if(offset + length > HEIS_FILE_MAX_SIZE)
    {
        retval = -EIO;
    } else {
        heisenbergfile_t *file = get_file(hfs, name);
        if(file) {
            printf("  write: %s: (%d)\n", file->name, file->observed);
            (void)observe_file(file, hWrite);
            memcpy(file->data + offset, buf, length);
            retval = length;
            if(offset + length > file->size)
                file->size = offset + length;
            //printf("  write: %s: (%zd)\n", name, file->size);
        }
    }
    return retval;
}
int   heisenberg_read (const char *name, char *buf, size_t length, off_t offset, struct fuse_file_info *finfo)
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    printf("  read: %s: %zd\n", name, hfs->file_count);
    heisenbergfile_t *file = get_file(hfs, name);
    if(file) {
        //printf("  read: %s: %s: %zd %ld (%ld)\n", name, file->name, offset, length, file->size);
        printf("  read: %s: (%d)\n", file->name, file->observed);
        (void)observe_file(file, hRead);
        if(length + offset > file->size)
        {
            length = file->size - offset;
            memcpy(buf, file->data + offset, length);
            retval = length;
        }
    }
    //printf("  read: retval: %d\n", retval);
    return retval;
}
int   heisenberg_truncate (const char *name, off_t new_size)
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    printf("  truncate: %s: %zd\n", name, hfs->file_count);
    heisenbergfile_t *file = get_file(hfs, name);
    if(file) {
        (void)observe_file(file, hTruncate);
        file->size = new_size;
        retval = 0;
    }
    return retval;
}
int   heisenberg_release (const char *name, struct fuse_file_info *finfo)
{
    int retval = -ENOENT;
    struct fuse_context *context = fuse_get_context();
    heisenbergfs_t *hfs = context->private_data;
    printf("  release: %s: %zd\n", name, hfs->file_count);
    heisenbergfile_t *file = get_file(hfs, name);
    if(file) {
        (void)observe_file(file, hRelease);
        retval = 0;
    }
    return retval;
}

int   heisenberg_rmdir (const char *);
int   heisenberg_rename (const char *, const char *);
int   heisenberg_unlink (const char *);
int   heisenberg_releasedir (const char *, struct fuse_file_info *);
static struct fuse_operations heisenberg_oper = {
    .init     = heisenberg_init,
    .destroy  = heisenberg_destroy,
    .readdir  = heisenberg_readdir,
    .getattr  = heisenberg_getattr,
    .create   = heisenberg_create,
    .open     = heisenberg_open,
    .utimens  = heisenberg_utimens,
    .mkdir    = heisenberg_mkdir,
    .write    = heisenberg_write,
    .read     = heisenberg_read,
    .truncate = heisenberg_truncate,
    .release  = heisenberg_release,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &heisenberg_oper, NULL);
}
