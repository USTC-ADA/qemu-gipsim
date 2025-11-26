#include "qemu-plugin.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb * tb);
static void vcpu_exec_cb(unsigned int vcpu_index, void * userdata);
static void plugin_before_exit_cb(qemu_plugin_id_t id, void * user_data);

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t * info, int argc, char ** argv) {
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);
    qemu_plugin_register_before_exit_cb(id, plugin_before_exit_cb, NULL);

    return 0;
}

static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb * tb) {
    size_t insn_count = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < insn_count; i++) {
        struct qemu_plugin_insn * insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_exec_cb, QEMU_PLUGIN_CB_R_REGS, NULL);
    }
}

static void vcpu_exec_cb(unsigned int vcpu_index, void * userdata) {
    uint64_t res;
    size_t size = qemu_plugin_read_gpr(vcpu_index, 1, (char *)&res, 8);
    printf("reg[1] read size: %zu, read value: %lu\n", size, res);
    fflush(stdout);
    sleep(1);
}

static void plugin_before_exit_cb(qemu_plugin_id_t id, void * user_data) {
    printf("Before exit callback\n");
    fflush(stdout);
}