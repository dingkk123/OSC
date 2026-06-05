#ifndef VFS_H
#define VFS_H

#include <stddef.h>

#define O_CREAT 00000100
#define VFS_MAX_FD 16
#define VFS_MAX_PATH 255
#define VFS_MAX_COMPONENT 63
#define SEEK_SET 0
#define FB_IOCTL_GET_INFO 0

enum vnode_type {
    VNODE_TYPE_DIR,
    VNODE_TYPE_FILE,
};

struct mount;
struct filesystem;
struct vnode;
struct file;

struct vnode {
    struct mount *mount;
    struct mount *mounted;
    struct vnode *parent;
    enum vnode_type type;
    struct vnode_operations *v_ops;
    struct file_operations *f_ops;
    void *internal;
};

struct file {
    struct vnode *vnode;
    size_t f_pos;
    struct file_operations *f_ops;
    int flags;
    int ref_count;
};

struct mount {
    struct vnode *root;
    struct vnode *mountpoint;
    struct filesystem *fs;
};

struct filesystem {
    const char *name;
    int (*setup_mount)(struct filesystem *fs, struct mount *mount);
};

struct file_operations {
    int (*open)(struct vnode *file_node, struct file **target);
    int (*close)(struct file *file);
    int (*read)(struct file *file, void *buf, size_t len);
    int (*write)(struct file *file, const void *buf, size_t len);
    long (*lseek64)(struct file *file, long offset, int whence);
    int (*ioctl)(struct file *file, unsigned long request, void *arg);
};

struct vnode_operations {
    int (*lookup)(struct vnode *dir_node,
                  struct vnode **target,
                  const char *component_name);
    int (*create)(struct vnode *dir_node,
                  struct vnode **target,
                  const char *component_name);
    int (*mkdir)(struct vnode *dir_node,
                 struct vnode **target,
                 const char *component_name);
};

extern struct mount *rootfs;

int vfs_init(void);
struct vnode *vfs_get_root(void);
int register_filesystem(struct filesystem *fs);

int vfs_lookup(const char *pathname, struct vnode **target);
int vfs_lookup_at(const char *pathname,
                  struct vnode *root,
                  struct vnode *cwd,
                  struct vnode **target);
int vfs_open(const char *pathname, int flags, struct file **target);
int vfs_open_at(const char *pathname,
                int flags,
                struct vnode *root,
                struct vnode *cwd,
                struct file **target);
int vfs_close(struct file *file);
int vfs_read(struct file *file, void *buf, size_t len);
int vfs_write(struct file *file, const void *buf, size_t len);
long vfs_lseek64(struct file *file, long offset, int whence);
int vfs_ioctl(struct file *file, unsigned long request, void *arg);
int vfs_mkdir(const char *pathname);
int vfs_mkdir_at(const char *pathname, struct vnode *root, struct vnode *cwd);
int vfs_mount(const char *target, const char *filesystem);
int vfs_mount_at(const char *target,
                 const char *filesystem,
                 struct vnode *root,
                 struct vnode *cwd);
int vfs_is_dir(struct vnode *node);
void vfs_file_ref(struct file *file);

int tmpfs_setup_mount(struct filesystem *fs, struct mount *mount);
int ramfs_setup_mount(struct filesystem *fs, struct mount *mount);
int devfs_setup_mount(struct filesystem *fs, struct mount *mount);

#endif

