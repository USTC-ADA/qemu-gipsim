#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>
#include <string.h>

#include "qemu/osdep.h"
#include "qemu/qemu-plugin.h"
#include "hw/core/cpu.h"
#include "target/riscv/cpu.h"

int qemu_plugin_read_gpr(unsigned int vcpu_index, unsigned int idx, char * buf, size_t size) {
    struct CPUState * cpu = qemu_get_cpu(vcpu_index);
    struct CPUArchState * cpu_arch_state = cpu_env(cpu);

    if (size <= 4) {
        memcpy(buf, cpu_arch_state->gpr + idx, size);
        return size;
    }
    else if (size <= 8) {
        memcpy(buf, cpu_arch_state->gpr + idx, 4);
        memcpy(buf + 4, cpu_arch_state->gprh + idx, size - 4);
        return size;
    }
    else { return 0; }
}

int qemu_plugin_read_fpr(unsigned int vcpu_index, unsigned int idx, char * buf, size_t size) {
    struct CPUState * cpu = qemu_get_cpu(vcpu_index);
    struct CPUArchState * cpu_arch_state = cpu_env(cpu);

    if (size <= 8) {
        memcpy(buf, cpu_arch_state->fpr + idx, size);
        return size;
    }
    else { return 0; }
}
