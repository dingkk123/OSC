#include "list.h"

static void __list_add(struct list_head* new_node,
                       struct list_head* prev,
                       struct list_head* next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

static void __list_del(struct list_head* prev, struct list_head* next) {
    next->prev = prev;
    prev->next = next;
}

void INIT_LIST_HEAD(struct list_head* list) {
    list->next = list;
    list->prev = list;
}

int list_empty(struct list_head* head) {
    return head->next == head;
}

void list_add(struct list_head* new_node, struct list_head* head) {
    __list_add(new_node, head, head->next);
}

void list_add_tail(struct list_head* new_node, struct list_head* head) {
    __list_add(new_node, head->prev, head);
}

void list_del(struct list_head* entry) {
    __list_del(entry->prev, entry->next);
}

void list_del_init(struct list_head* entry) {
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}
