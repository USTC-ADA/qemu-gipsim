#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t qemu_plugin_id_t;

typedef void (*qemu_plugin_udata_cb_t)(qemu_plugin_id_t id, void * userdata);

void qemu_plugin_register_before_exit_cb_impl(qemu_plugin_id_t id, qemu_plugin_udata_cb_t cb, void * udata);
void qemu_plugin_before_exit_cb(void);
void nya_exit(int);

typedef struct {
    qemu_plugin_udata_cb_t cb;
    qemu_plugin_id_t id;
    void * udata;
} UdataCb;

struct UdataCbList {
    size_t count, capacity;
    UdataCb * list;
};

struct UdataCbList before_exit_cb_list = { .count = 0, .capacity = 0, .list = NULL };

void qemu_plugin_register_before_exit_cb_impl(qemu_plugin_id_t id, qemu_plugin_udata_cb_t cb, void * udata) {
    if (!before_exit_cb_list.list) {
        before_exit_cb_list.list = malloc(sizeof(UdataCb) * 16);
        before_exit_cb_list.capacity = 16;
    }
    else if (before_exit_cb_list.count == before_exit_cb_list.capacity) {
        before_exit_cb_list.list =
            realloc(before_exit_cb_list.list, sizeof(UdataCb) * before_exit_cb_list.capacity * 2);
        before_exit_cb_list.capacity *= 2;
    }
    before_exit_cb_list.list[before_exit_cb_list.count].cb = cb;
    before_exit_cb_list.list[before_exit_cb_list.count].id = id;
    before_exit_cb_list.list[before_exit_cb_list.count].udata = udata;
    before_exit_cb_list.count++;
}

void qemu_plugin_before_exit_cb(void) {
    for (size_t i = 0; i < before_exit_cb_list.count; i++) {
        before_exit_cb_list.list[i].cb(before_exit_cb_list.list[i].id, before_exit_cb_list.list[i].udata);
    }
    free(before_exit_cb_list.list);
    before_exit_cb_list.count = 0;
    before_exit_cb_list.capacity = 0;
    before_exit_cb_list.list = NULL;
}

void nya_exit(int code) {
    qemu_plugin_before_exit_cb();
    exit(code);
}
