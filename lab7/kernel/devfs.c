#include "vfs.h"
#include "allocate.h"
#include "thread.h"
#include "uart.h"
#include "utils.h"
#include "video.h"

#define DEVFS_MAX_NAME 15
#define DEVFS_MAX_ENTRY 4

enum devfs_kind {
    DEVFS_KIND_DIR,
    DEVFS_KIND_UART,
    DEVFS_KIND_FB,
};

struct devfs_node {
    enum vnode_type type;
    enum devfs_kind kind;
    char name[DEVFS_MAX_NAME + 1];
    struct vnode *entry[DEVFS_MAX_ENTRY];
};

static int devfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int devfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name);
static int devfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name);

static int devfs_file_open(struct vnode *file_node, struct file **target);
static int devfs_file_close(struct file *file);
static int devfs_uart_read(struct file *file, void *buf, size_t len);
static int devfs_uart_write(struct file *file, const void *buf, size_t len);
static int devfs_fb_read(struct file *file, void *buf, size_t len);
static int devfs_fb_write(struct file *file, const void *buf, size_t len);
static long devfs_fb_lseek64(struct file *file, long offset, int whence);
static int devfs_fb_ioctl(struct file *file, unsigned long request, void *arg);

static struct vnode_operations devfs_vnode_ops = {
    .lookup = devfs_lookup,
    .create = devfs_create,
    .mkdir = devfs_mkdir,
};

static struct file_operations devfs_dir_file_ops = {
    .open = devfs_file_open,
    .close = devfs_file_close,
};

static struct file_operations devfs_uart_file_ops = {
    .open = devfs_file_open,
    .close = devfs_file_close,
    .read = devfs_uart_read,
    .write = devfs_uart_write,
};

static struct file_operations devfs_fb_file_ops = {
    .open = devfs_file_open,
    .close = devfs_file_close,
    .read = devfs_fb_read,
    .write = devfs_fb_write,
    .lseek64 = devfs_fb_lseek64,
    .ioctl = devfs_fb_ioctl,
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

static void copy_name(char *dst, const char *src) {
    int i = 0;

    while (i < DEVFS_MAX_NAME && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static struct file_operations *file_ops_for(enum devfs_kind kind) {
    if (kind == DEVFS_KIND_UART)
        return &devfs_uart_file_ops;
    if (kind == DEVFS_KIND_FB)
        return &devfs_fb_file_ops;

    return &devfs_dir_file_ops;
}

static struct vnode *devfs_create_vnode(enum vnode_type type,
                                        enum devfs_kind kind,
                                        const char *name,
                                        struct mount *mount) {
    struct vnode *vnode;
    struct devfs_node *node;

    vnode = allocate(sizeof(struct vnode));
    if (vnode == 0)
        return 0;

    node = allocate(sizeof(struct devfs_node));
    if (node == 0) {
        free(vnode);
        return 0;
    }

    kmemset_local(vnode, 0, sizeof(struct vnode));
    kmemset_local(node, 0, sizeof(struct devfs_node));

    node->type = type;
    node->kind = kind;
    if (name)
        copy_name(node->name, name);

    vnode->type = type;
    vnode->mount = mount;
    vnode->v_ops = &devfs_vnode_ops;
    vnode->f_ops = file_ops_for(kind);
    vnode->internal = node;
    return vnode;
}

static int devfs_add_child(struct vnode *parent, struct vnode *child) {
    struct devfs_node *dir = (struct devfs_node *)parent->internal;

    for (int i = 0; i < DEVFS_MAX_ENTRY; i++) {
        if (dir->entry[i] == 0) {
            child->parent = parent;
            child->mount = parent->mount;
            dir->entry[i] = child;
            return 0;
        }
    }

    return -1;
}

int devfs_setup_mount(struct filesystem *fs, struct mount *mount) {
    struct vnode *root;
    struct vnode *uart;
    struct vnode *fb;
    (void)fs;

    if (mount == 0)
        return -1;

    root = devfs_create_vnode(VNODE_TYPE_DIR, DEVFS_KIND_DIR, "", mount);
    uart = devfs_create_vnode(VNODE_TYPE_FILE, DEVFS_KIND_UART, "uart", mount);
    fb = devfs_create_vnode(VNODE_TYPE_FILE, DEVFS_KIND_FB, "fb", mount);

    if (root == 0 || uart == 0 || fb == 0)
        return -1;

    root->parent = root;
    mount->root = root;

    if (devfs_add_child(root, uart) < 0)
        return -1;
    if (devfs_add_child(root, fb) < 0)
        return -1;

    return 0;
}

static int devfs_lookup(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    struct devfs_node *dir;

    if (dir_node == 0 || target == 0 || component_name == 0)
        return -1;

    dir = (struct devfs_node *)dir_node->internal;
    if (dir == 0 || dir->type != VNODE_TYPE_DIR)
        return -1;

    for (int i = 0; i < DEVFS_MAX_ENTRY; i++) {
        struct vnode *child = dir->entry[i];
        struct devfs_node *child_node;

        if (child == 0)
            continue;

        child_node = (struct devfs_node *)child->internal;
        if (child_node && name_equal(child_node->name, component_name)) {
            *target = child;
            return 0;
        }
    }

    return -1;
}

static int devfs_create(struct vnode *dir_node,
                        struct vnode **target,
                        const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static int devfs_mkdir(struct vnode *dir_node,
                       struct vnode **target,
                       const char *component_name) {
    (void)dir_node;
    (void)target;
    (void)component_name;
    return -1;
}

static int devfs_file_open(struct vnode *file_node, struct file **target) {
    if (file_node == 0 || target == 0 || *target == 0)
        return -1;

    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

static int devfs_file_close(struct file *file) {
    free(file);
    return 0;
}

static int devfs_uart_read(struct file *file, void *buf, size_t len) {
    char *dst = (char *)buf;
    size_t i;
    (void)file;

    if (len == 0)
        return 0;
    if (buf == 0)
        return -1;

    for (i = 0; i < len; i++) {
        char c = uart_getc();

        while (c == 0) {
            schedule();
            c = uart_getc();
        }

        dst[i] = c;
    }

    return (int)i;
}

static int devfs_uart_write(struct file *file, const void *buf, size_t len) {
    const char *src = (const char *)buf;
    (void)file;

    if (len == 0)
        return 0;
    if (buf == 0)
        return -1;

    for (size_t i = 0; i < len; i++)
        uart_putc(src[i]);

    return (int)len;
}

static int devfs_fb_read(struct file *file, void *buf, size_t len) {
    (void)file;
    (void)buf;
    (void)len;
    return -1;
}

static int devfs_fb_write(struct file *file, const void *buf, size_t len) {
    long written;

    if (file == 0)
        return -1;

    written = video_framebuffer_write(file->f_pos, buf, len);
    if (written > 0)
        file->f_pos += written;

    return written;
}

static long devfs_fb_lseek64(struct file *file, long offset, int whence) {
    if (file == 0)
        return -1;
    if (whence != SEEK_SET || offset < 0)
        return -1;
    if ((unsigned long)offset > video_framebuffer_size())
        return -1;

    file->f_pos = offset;
    return offset;
}

static int devfs_fb_ioctl(struct file *file, unsigned long request, void *arg) {
    (void)file;

    if (request != FB_IOCTL_GET_INFO)
        return -1;

    return video_framebuffer_get_info((struct framebuffer_info *)arg);
}

