#include "vfs.h"
#include "allocate.h"
#include "utils.h"

#define TMPFS_MAX_NAME 15
#define TMPFS_MAX_DIR_ENTRY 16
#define TMPFS_MAX_FILE_SIZE 4096

struct tmpfs_node {
    enum vnode_type type;
    char name[TMPFS_MAX_NAME + 1];
    struct vnode *entry[TMPFS_MAX_DIR_ENTRY];
    char *data;
    size_t size;
};

static int tmpfs_open(struct vnode *file_node, struct file **target);
static int tmpfs_close(struct file *file);
static int tmpfs_read(struct file *file, void *buf, size_t len);
static int tmpfs_write(struct file *file, const void *buf, size_t len);
static long tmpfs_lseek64(struct file *file, long offset, int whence);
static int tmpfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int tmpfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int tmpfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name);

static struct file_operations tmpfs_file_ops = {
    .open = tmpfs_open,
    .close = tmpfs_close,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .lseek64 = tmpfs_lseek64,
    .ioctl = 0,
};

static struct vnode_operations tmpfs_vnode_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
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

static int name_valid(const char *name) {
    int len = 0;

    if (name == 0 || name[0] == '\0')
        return 0;

    while (name[len]) {
        len++;
        if (len > TMPFS_MAX_NAME)
            return 0;
    }

    return 1;
}

static void copy_name(char *dst, const char *src) {
    int i = 0;

    while (i < TMPFS_MAX_NAME && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static struct vnode *tmpfs_create_vnode(enum vnode_type type) {
    struct vnode *vnode;
    struct tmpfs_node *node;

    vnode = allocate(sizeof(struct vnode));
    if (vnode == 0)
        return 0;

    node = allocate(sizeof(struct tmpfs_node));
    if (node == 0) {
        free(vnode);
        return 0;
    }

    kmemset_local(vnode, 0, sizeof(struct vnode));
    kmemset_local(node, 0, sizeof(struct tmpfs_node));

    node->type = type;
    if (type == VNODE_TYPE_FILE) {
        node->data = allocate(TMPFS_MAX_FILE_SIZE);
        if (node->data == 0) {
            free(node);
            free(vnode);
            return 0;
        }
        kmemset_local(node->data, 0, TMPFS_MAX_FILE_SIZE);
    }

    vnode->type = type;
    vnode->v_ops = &tmpfs_vnode_ops;
    vnode->f_ops = &tmpfs_file_ops;
    vnode->internal = node;
    return vnode;
}

static int tmpfs_find_child(struct tmpfs_node *dir, const char *name, struct vnode **target) {
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        struct vnode *child = dir->entry[i];
        struct tmpfs_node *child_node;

        if (child == 0)
            continue;

        child_node = (struct tmpfs_node *)child->internal;
        if (child_node && name_equal(child_node->name, name)) {
            if (target)
                *target = child;
            return 0;
        }
    }

    return -1;
}

static int tmpfs_add_child(struct vnode *dir_node, struct vnode *child, const char *name) {
    struct tmpfs_node *dir = (struct tmpfs_node *)dir_node->internal;
    struct tmpfs_node *child_node = (struct tmpfs_node *)child->internal;

    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir->entry[i] == 0) {
            copy_name(child_node->name, name);
            child->parent = dir_node;
            child->mount = dir_node->mount;
            dir->entry[i] = child;
            return 0;
        }
    }

    return -1;
}

int tmpfs_setup_mount(struct filesystem *fs, struct mount *mount) {
    struct vnode *root;
    (void)fs;

    if (mount == 0)
        return -1;

    root = tmpfs_create_vnode(VNODE_TYPE_DIR);
    if (root == 0)
        return -1;

    root->mount = mount;
    root->parent = root;
    mount->root = root;
    return 0;
}

static int tmpfs_open(struct vnode *file_node, struct file **target) {
    if (file_node == 0 || target == 0 || *target == 0)
        return -1;

    (*target)->vnode = file_node;
    (*target)->f_ops = &tmpfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}

static int tmpfs_close(struct file *file) {
    free(file);
    return 0;
}

static int tmpfs_read(struct file *file, void *buf, size_t len) {
    struct tmpfs_node *node;
    size_t readable;

    if (file == 0)
        return -1;
    if (len == 0)
        return 0;
    if (buf == 0)
        return -1;

    node = (struct tmpfs_node *)file->vnode->internal;
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

static int tmpfs_write(struct file *file, const void *buf, size_t len) {
    struct tmpfs_node *node;
    size_t writable;

    if (file == 0)
        return -1;
    if (len == 0)
        return 0;
    if (buf == 0)
        return -1;

    node = (struct tmpfs_node *)file->vnode->internal;
    if (node == 0 || node->type != VNODE_TYPE_FILE)
        return -1;

    if (file->f_pos >= TMPFS_MAX_FILE_SIZE)
        return 0;

    writable = TMPFS_MAX_FILE_SIZE - file->f_pos;
    if (writable > len)
        writable = len;

    kmemcpy_local(node->data + file->f_pos, buf, writable);
    file->f_pos += writable;
    if (file->f_pos > node->size)
        node->size = file->f_pos;

    return (int)writable;
}

static long tmpfs_lseek64(struct file *file, long offset, int whence) {
    struct tmpfs_node *node;

    if (file == 0)
        return -1;
    if (whence != SEEK_SET || offset < 0)
        return -1;

    node = (struct tmpfs_node *)file->vnode->internal;
    if (node == 0 || node->type != VNODE_TYPE_FILE)
        return -1;
    if ((size_t)offset > node->size)
        return -1;

    file->f_pos = offset;
    return offset;
}

static int tmpfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    struct tmpfs_node *dir;

    if (dir_node == 0 || target == 0 || component_name == 0)
        return -1;

    dir = (struct tmpfs_node *)dir_node->internal;
    if (dir == 0 || dir->type != VNODE_TYPE_DIR)
        return -1;

    return tmpfs_find_child(dir, component_name, target);
}

static int tmpfs_create_common(struct vnode *dir_node,
                               struct vnode **target,
                               const char *component_name,
                               enum vnode_type type) {
    struct tmpfs_node *dir;
    struct vnode *child;

    if (dir_node == 0 || target == 0 || !name_valid(component_name))
        return -1;

    dir = (struct tmpfs_node *)dir_node->internal;
    if (dir == 0 || dir->type != VNODE_TYPE_DIR)
        return -1;

    if (tmpfs_find_child(dir, component_name, 0) == 0)
        return -1;

    child = tmpfs_create_vnode(type);
    if (child == 0)
        return -1;

    if (tmpfs_add_child(dir_node, child, component_name) < 0) {
        struct tmpfs_node *child_node = (struct tmpfs_node *)child->internal;

        if (child_node && child_node->data)
            free(child_node->data);
        if (child_node)
            free(child_node);
        free(child);
        return -1;
    }

    *target = child;
    return 0;
}

static int tmpfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    return tmpfs_create_common(dir_node, target, component_name, VNODE_TYPE_FILE);
}

static int tmpfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name) {
    return tmpfs_create_common(dir_node, target, component_name, VNODE_TYPE_DIR);
}

