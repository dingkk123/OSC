#include "vfs.h"
#include "allocate.h"
#include "initrd.h"
#include "utils.h"

#define RAMFS_MAX_NAME 63
#define RAMFS_MAX_DIR_ENTRY 32

extern unsigned long initrd_base;

struct ramfs_node {
    enum vnode_type type;
    char name[RAMFS_MAX_NAME + 1];
    struct vnode *entry[RAMFS_MAX_DIR_ENTRY];
    const char *data;
    size_t size;
};

static int ramfs_open(struct vnode *file_node, struct file **target);
static int ramfs_close(struct file *file);
static int ramfs_read(struct file *file, void *buf, size_t len);
static int ramfs_write(struct file *file, const void *buf, size_t len);
static long ramfs_lseek64(struct file *file, long offset, int whence);
static int ramfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int ramfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int ramfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name);

static struct file_operations ramfs_file_ops = {
    .open = ramfs_open,
    .close = ramfs_close,
    .read = ramfs_read,
    .write = ramfs_write,
    .lseek64 = ramfs_lseek64,
    .ioctl = 0,
};

static struct vnode_operations ramfs_vnode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
};

static int name_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }

    return *a == *b;
}

static unsigned long name_len(const char *name) {
    unsigned long len = 0;

    while (name[len])
        len++;

    return len;
}

static void copy_name_len(char *dst, const char *src, unsigned long len) {
    for (unsigned long i = 0; i < len; i++)
        dst[i] = src[i];
    dst[len] = '\0';
}

static struct vnode *ramfs_create_vnode(enum vnode_type type, struct mount *mount) {
    struct vnode *vnode;
    struct ramfs_node *node;

    vnode = allocate(sizeof(struct vnode));
    if (vnode == 0)
        return 0;

    node = allocate(sizeof(struct ramfs_node));
    if (node == 0) {
        free(vnode);
        return 0;
    }

    kmemset_local(vnode, 0, sizeof(struct vnode));
    kmemset_local(node, 0, sizeof(struct ramfs_node));

    node->type = type;
    vnode->type = type;
    vnode->mount = mount;
    vnode->v_ops = &ramfs_vnode_ops;
    vnode->f_ops = &ramfs_file_ops;
    vnode->internal = node;
    return vnode;
}

static int ramfs_find_child(struct vnode *dir_node, const char *name, struct vnode **target) {
    struct ramfs_node *dir = (struct ramfs_node *)dir_node->internal;

    for (int i = 0; i < RAMFS_MAX_DIR_ENTRY; i++) {
        struct vnode *child = dir->entry[i];
        struct ramfs_node *child_node;

        if (child == 0)
            continue;

        child_node = (struct ramfs_node *)child->internal;
        if (child_node && name_equal(child_node->name, name)) {
            if (target)
                *target = child;
            return 0;
        }
    }

    return -1;
}

static int ramfs_add_child(struct vnode *parent, struct vnode *child, const char *name) {
    struct ramfs_node *dir = (struct ramfs_node *)parent->internal;
    struct ramfs_node *child_node = (struct ramfs_node *)child->internal;
    unsigned long len = name_len(name);

    if (len == 0 || len > RAMFS_MAX_NAME)
        return -1;

    for (int i = 0; i < RAMFS_MAX_DIR_ENTRY; i++) {
        if (dir->entry[i] == 0) {
            copy_name_len(child_node->name, name, len);
            child->parent = parent;
            child->mount = parent->mount;
            dir->entry[i] = child;
            return 0;
        }
    }

    return -1;
}

static struct vnode *ramfs_get_or_create_dir(struct vnode *parent,
                                             const char *name,
                                             unsigned long len) {
    char component[RAMFS_MAX_NAME + 1];
    struct vnode *child;
    struct ramfs_node *child_node;

    if (len == 0 || len > RAMFS_MAX_NAME)
        return 0;

    copy_name_len(component, name, len);

    if (ramfs_find_child(parent, component, &child) == 0) {
        child_node = (struct ramfs_node *)child->internal;
        if (child_node && child_node->type == VNODE_TYPE_DIR)
            return child;
        return 0;
    }

    child = ramfs_create_vnode(VNODE_TYPE_DIR, parent->mount);
    if (child == 0)
        return 0;

    if (ramfs_add_child(parent, child, component) < 0) {
        free(child->internal);
        free(child);
        return 0;
    }

    return child;
}

static int ramfs_add_file(struct vnode *parent,
                          const char *name,
                          unsigned long len,
                          const char *data,
                          size_t size) {
    char component[RAMFS_MAX_NAME + 1];
    struct vnode *child;
    struct ramfs_node *node;

    if (len == 0 || len > RAMFS_MAX_NAME)
        return -1;

    copy_name_len(component, name, len);

    if (ramfs_find_child(parent, component, &child) == 0) {
        node = (struct ramfs_node *)child->internal;
        if (node == 0 || node->type != VNODE_TYPE_FILE)
            return -1;
        node->data = data;
        node->size = size;
        return 0;
    }

    child = ramfs_create_vnode(VNODE_TYPE_FILE, parent->mount);
    if (child == 0)
        return -1;

    node = (struct ramfs_node *)child->internal;
    node->data = data;
    node->size = size;

    if (ramfs_add_child(parent, child, component) < 0) {
        free(node);
        free(child);
        return -1;
    }

    return 0;
}

static void ramfs_add_cpio_entry(struct vnode *root,
                                 const char *path,
                                 const char *data,
                                 size_t size,
                                 int is_dir) {
    struct vnode *dir = root;
    const char *p = path;

    while (*p == '/' || (*p == '.' && p[1] == '/')) {
        if (*p == '/')
            p++;
        else
            p += 2;
    }

    while (*p) {
        const char *start;
        unsigned long len = 0;
        int last;

        while (*p == '/')
            p++;
        if (*p == '\0')
            return;

        start = p;
        while (p[len] && p[len] != '/')
            len++;

        p += len;
        while (*p == '/')
            p++;

        last = *p == '\0';
        if (last) {
            if (is_dir || (size == 0 && path[name_len(path) - 1] == '/'))
                (void)ramfs_get_or_create_dir(dir, start, len);
            else
                (void)ramfs_add_file(dir, start, len, data, size);
            return;
        }

        dir = ramfs_get_or_create_dir(dir, start, len);
        if (dir == 0)
            return;
    }
}

static void ramfs_load_initrd(struct vnode *root) {
    char *p;

    if (initrd_base == 0)
        return;

    p = (char *)initrd_base;
    while (1) {
        struct cpio_t *hdr = (struct cpio_t *)p;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int mode = hextoi(hdr->mode, 8);
        int header_size = align(sizeof(struct cpio_t) + namesize, 4);
        int data_size = align(filesize, 4);
        char *name = p + sizeof(struct cpio_t);
        char *data = p + header_size;
        int is_dir = (mode & 0170000) == 0040000;

        if (name_equal(name, "TRAILER!!!"))
            break;

        if (namesize > 1)
            ramfs_add_cpio_entry(root, name, data, (size_t)filesize, is_dir);

        p += header_size + data_size;
    }
}

int ramfs_setup_mount(struct filesystem *fs, struct mount *mount) {
    struct vnode *root;
    (void)fs;

    if (mount == 0)
        return -1;

    root = ramfs_create_vnode(VNODE_TYPE_DIR, mount);
    if (root == 0)
        return -1;

    root->parent = root;
    mount->root = root;
    ramfs_load_initrd(root);
    return 0;
}

static int ramfs_open(struct vnode *file_node, struct file **target) {
    if (file_node == 0 || target == 0 || *target == 0)
        return -1;

    (*target)->vnode = file_node;
    (*target)->f_ops = &ramfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}

static int ramfs_close(struct file *file) {
    free(file);
    return 0;
}

static int ramfs_read(struct file *file, void *buf, size_t len) {
    struct ramfs_node *node;
    size_t readable;

    if (file == 0)
        return -1;
    if (len == 0)
        return 0;
    if (buf == 0)
        return -1;

    node = (struct ramfs_node *)file->vnode->internal;
    if (node == 0 || node->type != VNODE_TYPE_FILE)
        return -1;

    if (file->f_pos >= node->size)
        return 0;

    readable = node->size - file->f_pos;
    if (readable > len)
        readable = len;

    kmemcpy_local(buf, node->data + file->f_pos, readable);
    file->f_pos += readable;
    return (int)readable;
}

static int ramfs_write(struct file *file, const void *buf, size_t len) {
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static long ramfs_lseek64(struct file *file, long offset, int whence) {
    struct ramfs_node *node;

    if (file == 0)
        return -1;
    if (whence != SEEK_SET || offset < 0)
        return -1;

    node = (struct ramfs_node *)file->vnode->internal;
    if (node == 0 || node->type != VNODE_TYPE_FILE)
        return -1;
    if ((size_t)offset > node->size)
        return -1;

    file->f_pos = offset;
    return offset;
}

static int ramfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    struct ramfs_node *dir;

    if (dir_node == 0 || target == 0 || component_name == 0)
        return -1;

    dir = (struct ramfs_node *)dir_node->internal;
    if (dir == 0 || dir->type != VNODE_TYPE_DIR)
        return -1;

    return ramfs_find_child(dir_node, component_name, target);
}

static int ramfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static int ramfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

