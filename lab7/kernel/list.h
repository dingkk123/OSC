#ifndef LIST_H
#define LIST_H

struct list_head {
    struct list_head* next;
    struct list_head* prev;
};

void INIT_LIST_HEAD(struct list_head* list);
int list_empty(struct list_head* head);
void list_add(struct list_head* new_node, struct list_head* head);
void list_add_tail(struct list_head* new_node, struct list_head* head);
void list_del(struct list_head* entry);
void list_del_init(struct list_head* entry);

#endif
