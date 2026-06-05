#include "vfs.h"
#include "allocate.h"
#include "utils.h"

#define MAX_FILESYSTEMS 16
#define MAX_MOUNT_DEPTH 16

struct mount *rootfs = 0;

static struct filesystem *filesystems[MAX_FILESYSTEMS];

static unsigned long kstrlen(const char *s) {
    unsigned long len = 0;

    if (s == 0)
        return 0;

    while (s[len])
        len++;

    return len;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }

    return *a - *b;
}

static void kstrcpy_len(char *dst, const char *src, unsigned long len) {
    for (unsigned long i = 0; i < len; i++)
        dst[i] = src[i];
    dst[len] = '\0';
}

static struct filesystem *find_filesystem(const char *name) {
    if (name == 0)
        return 0;

    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (filesystems[i] && kstrcmp(filesystems[i]->name, name) == 0)
            return filesystems[i];
    }

    return 0;
}

static struct vnode *follow_mounts(struct vnode *node) {
    int depth = 0;

    while (node && node->mounted && depth < MAX_MOUNT_DEPTH) {
        node = node->mounted->root;
        depth++;
    }

    return node;
}

static struct vnode *parent_of(struct vnode *node, struct vnode *root) {
    if (node == 0)
        return 0;

    if (root && node == root)
        return root;

    if (node->mount && node == node->mount->root && node->mount->mountpoint) {
        struct vnode *mountpoint = node->mount->mountpoint;

        if (root && mountpoint == root)
            return root;
        if (mountpoint->parent)
            return mountpoint->parent;
        return mountpoint;
    }

    if (node->parent)
        return node->parent;

    return node;
}

static int component_equal(const char *component, const char *name) {
    return kstrcmp(component, name) == 0;
}

static int split_parent_path(const char *pathname, char *parent_path, char *name) {
    unsigned long len;
    unsigned long end;
    unsigned long start;
    unsigned long name_len;

    if (pathname == 0 || parent_path == 0 || name == 0)
        return -1;

    len = kstrlen(pathname);
    if (len == 0 || len > VFS_MAX_PATH)
        return -1;

    end = len;
    while (end > 0 && pathname[end - 1] == '/')
        end--;

    if (end == 0)
        return -1;

    start = end;
    while (start > 0 && pathname[start - 1] != '/')
        start--;

    name_len = end - start;
    if (name_len == 0 || name_len > VFS_MAX_COMPONENT)
        return -1;

    kstrcpy_len(name, pathname + start, name_len);
    if (component_equal(name, ".") || component_equal(name, ".."))
        return -1;

    if (start == 0) {
        parent_path[0] = '\0';
        return 0;
    }

    if (start > VFS_MAX_PATH)
        return -1;

    kstrcpy_len(parent_path, pathname, start);
    return 0;
}

int register_filesystem(struct filesystem *fs) {
    if (fs == 0 || fs->name == 0 || fs->setup_mount == 0)
        return -1;

    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (filesystems[i] && kstrcmp(filesystems[i]->name, fs->name) == 0)
            return i;
    }

    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (filesystems[i] == 0) {
            filesystems[i] = fs;
            return i;
        }
    }

    return -1;
}

struct vnode *vfs_get_root(void) {
    if (rootfs == 0)
        return 0;

    return rootfs->root;
}

int vfs_is_dir(struct vnode *node) {
    node = follow_mounts(node);
    return node && node->type == VNODE_TYPE_DIR;
}

int vfs_lookup_at(const char *pathname,
                  struct vnode *root,
                  struct vnode *cwd,
                  struct vnode **target) {
    struct vnode *node;
    const char *p;
    unsigned long path_len;

    if (pathname == 0 || target == 0)
        return -1;

    path_len = kstrlen(pathname);
    if (path_len > VFS_MAX_PATH)
        return -1;

    if (root == 0) {
        if (rootfs == 0)
            return -1;
        root = rootfs->root;
    }

    if (cwd == 0)
        cwd = root;

    node = pathname[0] == '/' ? root : cwd;
    node = follow_mounts(node);
    p = pathname;

    while (*p) {
        char component[VFS_MAX_COMPONENT + 1];
        unsigned long len = 0;

        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        while (p[len] && p[len] != '/')
            len++;
        if (len == 0 || len > VFS_MAX_COMPONENT)
            return -1;

        kstrcpy_len(component, p, len);

        if (component_equal(component, ".")) {
        } else if (component_equal(component, "..")) {
            node = parent_of(node, root);
        } else {
            struct vnode *next;

            node = follow_mounts(node);
            if (node == 0 || node->type != VNODE_TYPE_DIR || node->v_ops == 0 || node->v_ops->lookup == 0)
                return -1;

            if (node->v_ops->lookup(node, &next, component) < 0)
                return -1;

            node = follow_mounts(next);
        }

        p += len;
    }

    *target = node;
    return node ? 0 : -1;
}

int vfs_lookup(const char *pathname, struct vnode **target) {
    struct vnode *root = vfs_get_root();

    return vfs_lookup_at(pathname, root, root, target);
}

int vfs_open_at(const char *pathname,
                int flags,
                struct vnode *root,
                struct vnode *cwd,
                struct file **target) {
    struct vnode *node = 0;
    struct file *file;
    int ret;

    if (target == 0)
        return -1;

    *target = 0;

    ret = vfs_lookup_at(pathname, root, cwd, &node);
    if (ret < 0) {
        char parent_path[VFS_MAX_PATH + 1];
        char name[VFS_MAX_COMPONENT + 1];
        struct vnode *parent = 0;

        if ((flags & O_CREAT) == 0)
            return -1;

        if (split_parent_path(pathname, parent_path, name) < 0)
            return -1;

        if (vfs_lookup_at(parent_path, root, cwd, &parent) < 0)
            return -1;

        parent = follow_mounts(parent);
        if (parent == 0 || parent->type != VNODE_TYPE_DIR || parent->v_ops == 0 || parent->v_ops->create == 0)
            return -1;

        if (parent->v_ops->create(parent, &node, name) < 0)
            return -1;
    }

    node = follow_mounts(node);
    if (node == 0 || node->f_ops == 0 || node->f_ops->open == 0)
        return -1;

    file = allocate(sizeof(struct file));
    if (file == 0)
        return -1;

    kmemset_local(file, 0, sizeof(struct file));
    file->flags = flags;
    file->ref_count = 1;

    ret = node->f_ops->open(node, &file);
    if (ret < 0) {
        free(file);
        return ret;
    }

    file->flags = flags;
    file->ref_count = 1;
    *target = file;
    return 0;
}

int vfs_open(const char *pathname, int flags, struct file **target) {
    struct vnode *root = vfs_get_root();

    return vfs_open_at(pathname, flags, root, root, target);
}

void vfs_file_ref(struct file *file) {
    if (file)
        file->ref_count++;
}

int vfs_close(struct file *file) {
    if (file == 0)
        return -1;

    if (file->ref_count > 1) {
        file->ref_count--;
        return 0;
    }

    if (file->f_ops && file->f_ops->close)
        return file->f_ops->close(file);

    free(file);
    return 0;
}

int vfs_read(struct file *file, void *buf, size_t len) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->read == 0)
        return -1;

    return file->f_ops->read(file, buf, len);
}

int vfs_write(struct file *file, const void *buf, size_t len) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->write == 0)
        return -1;

    return file->f_ops->write(file, buf, len);
}

long vfs_lseek64(struct file *file, long offset, int whence) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->lseek64 == 0)
        return -1;

    return file->f_ops->lseek64(file, offset, whence);
}

int vfs_ioctl(struct file *file, unsigned long request, void *arg) {
    if (file == 0 || file->f_ops == 0 || file->f_ops->ioctl == 0)
        return -1;

    return file->f_ops->ioctl(file, request, arg);
}

int vfs_mkdir_at(const char *pathname, struct vnode *root, struct vnode *cwd) {
    char parent_path[VFS_MAX_PATH + 1];
    char name[VFS_MAX_COMPONENT + 1];
    struct vnode *parent;
    struct vnode *created;

    if (split_parent_path(pathname, parent_path, name) < 0)
        return -1;

    if (vfs_lookup_at(parent_path, root, cwd, &parent) < 0)
        return -1;

    parent = follow_mounts(parent);
    if (parent == 0 || parent->type != VNODE_TYPE_DIR || parent->v_ops == 0 || parent->v_ops->mkdir == 0)
        return -1;

    return parent->v_ops->mkdir(parent, &created, name);
}

int vfs_mkdir(const char *pathname) {
    struct vnode *root = vfs_get_root();

    return vfs_mkdir_at(pathname, root, root);
}

int vfs_mount_at(const char *target,
                 const char *filesystem,
                 struct vnode *root,
                 struct vnode *cwd) {
    struct filesystem *fs;
    struct mount *mount;
    struct vnode *mountpoint;

    fs = find_filesystem(filesystem);
    if (fs == 0)
        return -1;

    if (vfs_lookup_at(target, root, cwd, &mountpoint) < 0)
        return -1;

    mountpoint = follow_mounts(mountpoint);
    if (mountpoint == 0 || mountpoint->type != VNODE_TYPE_DIR || mountpoint->mounted)
        return -1;

    mount = allocate(sizeof(struct mount));
    if (mount == 0)
        return -1;

    kmemset_local(mount, 0, sizeof(struct mount));
    mount->mountpoint = mountpoint;
    mount->fs = fs;

    if (fs->setup_mount(fs, mount) < 0 || mount->root == 0) {
        free(mount);
        return -1;
    }

    mount->root->mount = mount;
    mount->root->parent = mount->root;
    mountpoint->mounted = mount;
    return 0;
}

int vfs_mount(const char *target, const char *filesystem) {
    struct vnode *root = vfs_get_root();

    return vfs_mount_at(target, filesystem, root, root);
}

int vfs_init(void) {
    static struct filesystem tmpfs = {
        .name = "tmpfs",
        .setup_mount = tmpfs_setup_mount,
    };
    static struct filesystem ramfs = {
        .name = "ramfs",
        .setup_mount = ramfs_setup_mount,
    };
    static struct filesystem devfs = {
        .name = "devfs",
        .setup_mount = devfs_setup_mount,
    };
    struct filesystem *root_fs;

    if (rootfs)
        return 0;

    if (register_filesystem(&tmpfs) < 0)
        return -1;
    if (register_filesystem(&ramfs) < 0)
        return -1;
    if (register_filesystem(&devfs) < 0)
        return -1;

    root_fs = find_filesystem("tmpfs");
    if (root_fs == 0)
        return -1;

    rootfs = allocate(sizeof(struct mount));
    if (rootfs == 0)
        return -1;

    kmemset_local(rootfs, 0, sizeof(struct mount));
    rootfs->fs = root_fs;

    if (root_fs->setup_mount(root_fs, rootfs) < 0 || rootfs->root == 0) {
        free(rootfs);
        rootfs = 0;
        return -1;
    }

    rootfs->root->mount = rootfs;
    rootfs->root->parent = rootfs->root;

    if (vfs_mkdir("/ramfs") < 0)
        return -1;

    if (vfs_mount("/ramfs", "ramfs") < 0)
        return -1;

    if (vfs_mkdir("/dev") < 0)
        return -1;

    if (vfs_mount("/dev", "devfs") < 0)
        return -1;

    return 0;
}

