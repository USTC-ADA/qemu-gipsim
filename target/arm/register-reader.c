#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>
#include <string.h>

#include "qemu/osdep.h"
#include "qemu/qemu-plugin.h"
#include "hw/core/cpu.h"
#include "target/arm/cpu.h"

int qemu_plugin_read_gpr(unsigned int vcpu_index, unsigned int idx, char * buf, size_t size) {
    struct CPUState * cpu = qemu_get_cpu(vcpu_index);
    CPUARMState * cpu_arch_state = cpu_env(cpu);

    size_t ret = size <= 8 ? size: 8;
    memcpy(buf, cpu_arch_state->xregs + idx, ret);
    return ret;
}
