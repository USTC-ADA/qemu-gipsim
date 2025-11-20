#!/usr/bin/env bash

set -eu
set -o pipefail

BINARY_PATH="./build"

if [ $# -gt 0 ]; then
    BINARY_PATH=$1
fi

(
    objdump -d "${BINARY_PATH}/qemu-system-riscv64" \
        | grep -E "call.*<exit@plt>" \
        | awk '{ print $1 }' \
        | sed -E 's/^(.*):$/0x\1/' \
        | xargs -n1 addr2line -e "${BINARY_PATH}/qemu-system-riscv64" \
        | xargs -n1 realpath
    objdump -d "${BINARY_PATH}/qemu-system-x86_64" \
        | grep -E "call.*<exit@plt>" \
        | awk '{ print $1 }' \
        | sed -E 's/^(.*):$/0x\1/' \
        | xargs -n1 addr2line -e "${BINARY_PATH}/qemu-system-x86_64" \
        | xargs -n1 realpath
    objdump -d "${BINARY_PATH}/qemu-system-aarch64" \
        | grep -E "call.*<exit@plt>" \
        | awk '{ print $1 }' \
        | sed -E 's/^(.*):$/0x\1/' \
        | xargs -n1 addr2line -e "${BINARY_PATH}/qemu-system-aarch64" \
        | xargs -n1 realpath
) \
| sort -t: -k1,1 -k2,2nr \
| uniq \
| tee exit_positions \
| (
    while IFS=: read -r file line; do
        sed -i -E "
        ${line}{
            s/^([[:space:]]*)exit(.*)$/\
\1{\n\
\1    extern void nya_exit(int);\n\
\1    nya_exit\2\n\
\1    exit\2\n\
\1}/
        }
        " "${file}"
    done
)
