#ifndef PL_LIST_H
#define PL_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef ptrdiff_t isize;
typedef size_t    usize;

/**
 *\struct ListNode
 *\brief 链表节点结构
 */
typedef struct list *pl_list_t;
struct list {
    union {
        void *data;
        isize idata;
        usize udata;
    };
    pl_list_t prev;
    pl_list_t next;
};

/**
 *\brief 创建一个新的链表节点
 *\param[in] data 节点数据
 *\return 新的链表节点指针
 */
static pl_list_t pl_list_alloc(void *data);

/**
 *\brief 删除整个链表
 *\param[in] list 链表头指针
 *\return 恒为 NULL
 */
static pl_list_t pl_list_free(pl_list_t list);

/**
 *\brief 删除整个链表
 *\param[in] list 链表头指针
 *\param[in] free_data 释放数据的 callback
 *\return 恒为 NULL
 */
static pl_list_t pl_list_free_with(pl_list_t list, void (*free_data)(void *));

/**
 *\brief 在链表末尾插入节点
 *\param[in] list 链表头指针
 *\param[in] data 节点数据
 *\return 更新后的链表头指针
 */
static pl_list_t pl_list_append(pl_list_t list, void *data);

/**
 *\brief 获取链表头
 *\param[in] list 链表指针
 *\return 存在则为指向链表头的指针，否则为 NULL
 */
static pl_list_t pl_list_head(pl_list_t list);

/**
 *\brief 获取链表尾
 *\param[in] list 链表指针
 *\return 存在则为指向链表尾的指针，否则为 NULL
 */
static pl_list_t pl_list_tail(pl_list_t list);

/**
 *\brief 获取链表的第 n 项
 *\param[in] list 链表指针 (最好是头指针)
 *\param[in] n 序号 (从 0 开始)
 *\return 存在则为指向该项的指针，否则为 NULL
 */
static pl_list_t pl_list_nth(pl_list_t list, size_t n);

/**
 *\brief 获取链表的倒数第 n 项
 *\param[in] list 链表指针 (最好是尾指针)
 *\param[in] n 序号 (从 0 开始倒数)
 *\return 存在则为指向该项的指针，否则为 NULL
 */
static pl_list_t pl_list_nth_last(pl_list_t list, size_t n);

/**
 *\brief 在链表开头插入节点
 *\param[in] list 链表头指针
 *\param[in] data 节点数据
 *\return 更新后的链表头指针
 */
static pl_list_t pl_list_prepend(pl_list_t list, void *data);

/**
 *\brief 在链表中查找节点
 *\param[in] list 链表头指针
 *\param[in] data 要查找的节点数据
 *\return 若找到对应节点，则返回true，否则返回false
 */
static bool pl_list_search(pl_list_t list, void *data);

/**
 *\brief 删除链表中的节点
 *\param[in] list 链表头指针
 *\param[in] data 要删除的节点数据
 *\return 更新后的链表头指针
 */
static pl_list_t pl_list_delete(pl_list_t list, void *data);

/**
 *\brief 删除链表中的节点
 *\param[in] slist 链表头指针
 *\param[in] node 要删除的节点
 *\return 更新后的链表头指针
 */
static pl_list_t pl_list_delete_node(pl_list_t list, pl_list_t node);

/**
 *\brief 链表的长度
 *\param[in] list 链表头指针
 *\return 链表的长度
 */
static size_t pl_list_length(pl_list_t list);

/**
 *\brief 打印链表中的节点数据
 *\param[in] list 链表头指针
 */
static void pl_list_print(pl_list_t list);

static pl_list_t pl_list_alloc(void *data) {
    pl_list_t node = (pl_list_t)malloc(sizeof(*node));
    if (node == NULL) return NULL;
    node->data = data;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

static pl_list_t pl_list_free(pl_list_t list) {
    while (list != NULL) {
        pl_list_t next = list->next;
        free(list);
        list = next;
    }
    return NULL;
}
static pl_list_t pl_list_free_with(pl_list_t list, void (*free_data)(void *)) {
    while (list != NULL) {
        pl_list_t next = list->next;
        free_data(list->data);
        free(list);
        list = next;
    }
    return NULL;
}

static pl_list_t pl_list_append(pl_list_t list, void *data) {
    pl_list_t node = pl_list_alloc(data);
    if (node == NULL) return list;

    if (list == NULL) {
        list = node;
    } else {
        pl_list_t current = list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = node;
        node->prev    = current;
    }

    return list;
}

static pl_list_t pl_list_prepend(pl_list_t list, void *data) {
    pl_list_t node = pl_list_alloc(data);
    if (node == NULL) return list;

    node->next = list;
    if (list != NULL) list->prev = node;
    list = node;

    return list;
}

static void *pl_list_pop(pl_list_t *pl_list_p) {
    if (pl_list_p == NULL || *pl_list_p == NULL) return NULL;
    pl_list_t list = pl_list_tail(*pl_list_p);
    if (*pl_list_p == list) *pl_list_p = list->prev;
    if (list->prev) list->prev->next = NULL;
    pl_list_t data = (pl_list_t)list->data;
    free(list);
    return data;
}

static pl_list_t pl_list_head(pl_list_t list) {
    if (list == NULL) return NULL;
    for (; list->prev; list = list->prev) {}
    return list;
}

static pl_list_t pl_list_tail(pl_list_t list) {
    if (list == NULL) return NULL;
    for (; list->next; list = list->next) {}
    return list;
}

static pl_list_t pl_list_nth(pl_list_t list, size_t n) {
    if (list == NULL) return NULL;
    list = pl_list_head(list);
    for (size_t i = 0; i < n; i++) {
        list = list->next;
        if (list == NULL) return NULL;
    }
    return list;
}

static pl_list_t pl_list_nth_last(pl_list_t list, size_t n) {
    if (list == NULL) return NULL;
    list = pl_list_tail(list);
    for (size_t i = 0; i < n; i++) {
        list = list->prev;
        if (list == NULL) return NULL;
    }
    return list;
}

static bool pl_list_search(pl_list_t list, void *data) {
    pl_list_t current = list;
    while (current != NULL) {
        if (current->data == data) return true;
        current = current->next;
    }
    return false;
}

static pl_list_t pl_list_delete(pl_list_t list, void *data) {
    if (list == NULL) return NULL;

    if (list->data == data) {
        pl_list_t temp = list;
        list        = list->next;
        free(temp);
        return list;
    }

    for (pl_list_t current = list->next; current; current = current->next) {
        if (current->data == data) {
            current->prev->next = current->next;
            if (current->next != NULL) current->next->prev = current->prev;
            free(current);
            break;
        }
    }

    return list;
}

static pl_list_t pl_list_delete_with(pl_list_t list, void *data, void (*callback)(void *)) {
    if (list == NULL) return NULL;

    if (list->data == data) {
        pl_list_t temp = list;
        list        = list->next;
        if (callback) callback(temp->data);
        free(temp);
        return list;
    }

    for (pl_list_t current = list->next; current; current = current->next) {
        if (current->data == data) {
            current->prev->next = current->next;
            if (current->next != NULL) current->next->prev = current->prev;
            if (callback) callback(current->data);
            free(current);
            break;
        }
    }

    return list;
}

static pl_list_t pl_list_delete_node(pl_list_t list, pl_list_t node) {
    if (list == NULL || node == NULL) return list;

    if (list == node) {
        pl_list_t temp = list;
        list        = list->next;
        free(temp);
        return list;
    }

    node->prev->next = node->next;
    if (node->next != NULL) node->next->prev = node->prev;
    free(node);
    return list;
}

static pl_list_t pl_list_delete_node_with(pl_list_t list, pl_list_t node, void (*callback)(void *)) {
    if (list == NULL || node == NULL) return list;

    if (list == node) {
        pl_list_t temp = list;
        list        = list->next;
        if (callback) callback(temp->data);
        free(temp);
        return list;
    }

    node->prev->next = node->next;
    if (node->next != NULL) node->next->prev = node->prev;
    if (callback) callback(node->data);
    free(node);
    return list;
}

static size_t pl_list_length(pl_list_t list) {
    size_t count   = 0;
    pl_list_t current = list;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    return count;
}

/**
 *\brief 在链表末尾插入节点
 *\param[in,out] list 链表头指针
 *\param[in] data 节点数据
 */
#define pl_list_append(list, data) ((list) = pl_list_append(list, (void *)(data)))

/**
 *\brief 在链表开头插入节点
 *\param[in,out] list 链表头指针
 *\param[in] data 节点数据
 */
#define pl_list_prepend(list, data) ((list) = pl_list_prepend(list, (void *)(data)))

#define pl_list_push(list, data) pl_list_append(list, data)
#define pl_list_pop(list)        pl_list_pop(&(list))

#define pl_list_popi(list) ((usize)pl_list_pop(list))
#define pl_list_popu(list) ((isize)pl_list_pop(list))

/**
 *\brief 删除链表中的节点
 *\param[in,out] list 链表头指针
 *\param[in] data 要删除的节点数据
 */
#define pl_list_delete(list, data) ((list) = pl_list_delete(list, data))

#define pl_list_delete_with(list, data, callback) ((list) = pl_list_delete_with(list, data, callback))

/**
 *\brief 删除链表中的节点
 *\param[in,out] list 链表头指针
 *\param[in] node 要删除的节点
 */
#define pl_list_delete_node(slist, node) ((slist) = pl_list_delete_node(slist, node))

#define pl_list_delete_node_with(list, node, callback)                                                \
    ((list) = pl_list_delete_node_with(list, node, callback))

/**
 *\brief 遍历链表中的节点并执行操作
 *\param[in] list 链表头指针
 *\param[in] node 用于迭代的节点指针变量
 */
#define pl_list_foreach(list, node) for (pl_list_t node = (list); node; node = node->next)

/**
 *\brief 遍历链表中的节点并执行操作
 *\param[in] list 链表头指针
 *\param[in] node 用于迭代的节点指针变量
 */
#define pl_list_foreach_cnt(list, i, node, code)                                                      \
    ({                                                                                             \
        size_t i = 0;                                                                              \
        for (pl_list_t node = (list); node; (node) = (node)->next, (i)++)                             \
            (code);                                                                                \
    })

#define pl_list_first_node(list, node, expr)                                                          \
    ({                                                                                             \
        pl_list_t _match_ = NULL;                                                                     \
        for (pl_list_t node = (list); node; node = node->next) {                                      \
            if ((expr)) {                                                                          \
                _match_ = node;                                                                    \
                break;                                                                             \
            }                                                                                      \
        }                                                                                          \
        _match_;                                                                                   \
    })

#define pl_list_first(list, _data_, expr)                                                             \
    ({                                                                                             \
        void *_match_ = NULL;                                                                      \
        for (pl_list_t node = (list); node; node = node->next) {                                      \
            void *_data_ = node->data;                                                             \
            if ((expr)) {                                                                          \
                _match_ = _data_;                                                                  \
                break;                                                                             \
            }                                                                                      \
        }                                                                                          \
        _match_;                                                                                   \
    })

#endif /* PL_LIST_H */
