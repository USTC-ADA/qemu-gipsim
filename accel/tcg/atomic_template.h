/*
 * Atomic helper templates
 * Included from tcg-runtime.c and cputlb.c.
 *
 * Copyright (c) 2016 Red Hat, Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/plugin.h"

#if DATA_SIZE == 16
# define SUFFIX     o
# define DATA_TYPE  Int128
# define BSWAP      bswap128
# define SHIFT      4
#elif DATA_SIZE == 8
# define SUFFIX     q
# define DATA_TYPE  aligned_uint64_t
# define SDATA_TYPE aligned_int64_t
# define BSWAP      bswap64
# define SHIFT      3
#elif DATA_SIZE == 4
# define SUFFIX     l
# define DATA_TYPE  uint32_t
# define SDATA_TYPE int32_t
# define BSWAP      bswap32
# define SHIFT      2
#elif DATA_SIZE == 2
# define SUFFIX     w
# define DATA_TYPE  uint16_t
# define SDATA_TYPE int16_t
# define BSWAP      bswap16
# define SHIFT      1
#elif DATA_SIZE == 1
# define SUFFIX     b
# define DATA_TYPE  uint8_t
# define SDATA_TYPE int8_t
# define BSWAP
# define SHIFT      0
#else
# error unsupported data size
#endif

#if DATA_SIZE >= 4
# define ABI_TYPE  DATA_TYPE
#else
# define ABI_TYPE  uint32_t
#endif

#if DATA_SIZE == 16
# define VALUE_LOW(V) int128_getlo(V)
# define VALUE_HIGH(V) int128_gethi(V)
# define VALUE_EQUAL(A, B) int128_eq(A, B)
#else
# define VALUE_LOW(V) ((uint64_t)(V))
# define VALUE_HIGH(V) 0
# define VALUE_EQUAL(A, B) ((A) == (B))
#endif

#define TRACE_RMW_SUCCESS(OLD, NEW, SUCCESS)                          \
    atomic_trace_rmw_post(env, addr,                                  \
                          VALUE_LOW(OLD), VALUE_HIGH(OLD),             \
                          VALUE_LOW(NEW), VALUE_HIGH(NEW), SUCCESS, oi)
#define TRACE_RMW(OLD, NEW) TRACE_RMW_SUCCESS(OLD, NEW, true)

/* Define host-endian atomic operations.  Note that END is used within
   the ATOMIC_NAME macro, and redefined below.  */
#if DATA_SIZE == 1
# define END
#elif HOST_BIG_ENDIAN
# define END  _be
#else
# define END  _le
#endif

ABI_TYPE ATOMIC_NAME(cmpxchg)(CPUArchState *env, abi_ptr addr,
                              ABI_TYPE cmpv, ABI_TYPE newv,
                              MemOpIdx oi, uintptr_t retaddr)
{
    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr);
    DATA_TYPE ret;

#if DATA_SIZE == 16
    ret = atomic16_cmpxchg(haddr, cmpv, newv);
#else
    ret = qatomic_cmpxchg__nocheck(haddr, cmpv, newv);
#endif
    ATOMIC_MMU_CLEANUP;
    DATA_TYPE written = VALUE_EQUAL(ret, cmpv) ? newv : ret;
    TRACE_RMW_SUCCESS(ret, written, VALUE_EQUAL(ret, cmpv));
    return ret;
}

#if DATA_SIZE < 16
ABI_TYPE ATOMIC_NAME(xchg)(CPUArchState *env, abi_ptr addr, ABI_TYPE val,
                           MemOpIdx oi, uintptr_t retaddr)
{
    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr);
    DATA_TYPE ret;

    ret = qatomic_xchg__nocheck(haddr, val);
    ATOMIC_MMU_CLEANUP;
    TRACE_RMW(ret, (DATA_TYPE)val);
    return ret;
}

/*
 * These helpers are, as a whole, full barriers.  Within the helper,
 * the leading barrier is explicit and the trailing barrier is within
 * cmpxchg primitive.
 *
 * Trace this load + RMW loop as a single RMW op. This way, regardless
 * of CF_PARALLEL's value, we'll trace just a read and a write.
 */
#define GEN_ATOMIC_HELPER_FN(X, FN, XDATA_TYPE, RET)                \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, abi_ptr addr,            \
                        ABI_TYPE xval, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    XDATA_TYPE *haddr, cmp, old, new, val = xval;                   \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE, retaddr);   \
    smp_mb();                                                       \
    cmp = qatomic_read__nocheck(haddr);                             \
    do {                                                            \
        old = cmp; new = FN(old, val);                              \
        cmp = qatomic_cmpxchg__nocheck(haddr, old, new);            \
    } while (cmp != old);                                           \
    ATOMIC_MMU_CLEANUP;                                             \
    TRACE_RMW(old, new);                                            \
    return RET;                                                     \
}

GEN_ATOMIC_HELPER_FN(fetch_smin, MIN, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umin, MIN,  DATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_smax, MAX, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umax, MAX,  DATA_TYPE, old)

GEN_ATOMIC_HELPER_FN(smin_fetch, MIN, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umin_fetch, MIN,  DATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(smax_fetch, MAX, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umax_fetch, MAX,  DATA_TYPE, new)

#define ADD(X, Y) ((X) + (Y))
#define AND(X, Y) ((X) & (Y))
#define OR(X, Y)  ((X) | (Y))
#define XOR(X, Y) ((X) ^ (Y))

#define GEN_ATOMIC_FETCH_HELPER(NAME, ATOMIC, OP, RET)               \
ABI_TYPE ATOMIC_NAME(NAME)(CPUArchState *env, abi_ptr addr,          \
                           ABI_TYPE val, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    DATA_TYPE *haddr, old, new;                                     \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE, retaddr); \
    old = qatomic_fetch_##ATOMIC(haddr, val);                        \
    new = OP(old, (DATA_TYPE)val);                                  \
    ATOMIC_MMU_CLEANUP;                                             \
    TRACE_RMW(old, new);                                            \
    return RET;                                                     \
}

GEN_ATOMIC_FETCH_HELPER(fetch_add, add, ADD, old)
GEN_ATOMIC_FETCH_HELPER(fetch_and, and, AND, old)
GEN_ATOMIC_FETCH_HELPER(fetch_or, or, OR, old)
GEN_ATOMIC_FETCH_HELPER(fetch_xor, xor, XOR, old)
GEN_ATOMIC_FETCH_HELPER(add_fetch, add, ADD, new)
GEN_ATOMIC_FETCH_HELPER(and_fetch, and, AND, new)
GEN_ATOMIC_FETCH_HELPER(or_fetch, or, OR, new)
GEN_ATOMIC_FETCH_HELPER(xor_fetch, xor, XOR, new)

#undef GEN_ATOMIC_FETCH_HELPER

#undef ADD
#undef AND
#undef OR
#undef XOR

#undef GEN_ATOMIC_HELPER_FN
#endif /* DATA SIZE < 16 */

#undef END

#if DATA_SIZE > 1

/* Define reverse-host-endian atomic operations.  Note that END is used
   within the ATOMIC_NAME macro.  */
#if HOST_BIG_ENDIAN
# define END  _le
#else
# define END  _be
#endif

ABI_TYPE ATOMIC_NAME(cmpxchg)(CPUArchState *env, abi_ptr addr,
                              ABI_TYPE cmpv, ABI_TYPE newv,
                              MemOpIdx oi, uintptr_t retaddr)
{
    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr);
    DATA_TYPE ret;

#if DATA_SIZE == 16
    ret = atomic16_cmpxchg(haddr, BSWAP(cmpv), BSWAP(newv));
#else
    ret = qatomic_cmpxchg__nocheck(haddr, BSWAP(cmpv), BSWAP(newv));
#endif
    ATOMIC_MMU_CLEANUP;
    DATA_TYPE old = BSWAP(ret);
    DATA_TYPE written = VALUE_EQUAL(old, cmpv) ? newv : old;
    TRACE_RMW_SUCCESS(old, written, VALUE_EQUAL(old, cmpv));
    return old;
}

#if DATA_SIZE < 16
ABI_TYPE ATOMIC_NAME(xchg)(CPUArchState *env, abi_ptr addr, ABI_TYPE val,
                           MemOpIdx oi, uintptr_t retaddr)
{
    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr);
    ABI_TYPE ret;

    ret = qatomic_xchg__nocheck(haddr, BSWAP(val));
    ATOMIC_MMU_CLEANUP;
    DATA_TYPE old = BSWAP(ret);
    TRACE_RMW(old, (DATA_TYPE)val);
    return old;
}

/* These helpers are, as a whole, full barriers.  Within the helper,
 * the leading barrier is explicit and the trailing barrier is within
 * cmpxchg primitive.
 *
 * Trace this load + RMW loop as a single RMW op. This way, regardless
 * of CF_PARALLEL's value, we'll trace just a read and a write.
 */
#define GEN_ATOMIC_HELPER_FN(X, FN, XDATA_TYPE, RET)                \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, abi_ptr addr,            \
                        ABI_TYPE xval, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    XDATA_TYPE *haddr, ldo, ldn, old, new, val = xval;              \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE, retaddr);   \
    smp_mb();                                                       \
    ldn = qatomic_read__nocheck(haddr);                             \
    do {                                                            \
        ldo = ldn; old = BSWAP(ldo); new = FN(old, val);            \
        ldn = qatomic_cmpxchg__nocheck(haddr, ldo, BSWAP(new));     \
    } while (ldo != ldn);                                           \
    ATOMIC_MMU_CLEANUP;                                             \
    TRACE_RMW(old, new);                                            \
    return RET;                                                     \
}

GEN_ATOMIC_HELPER_FN(fetch_smin, MIN, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umin, MIN,  DATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_smax, MAX, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umax, MAX,  DATA_TYPE, old)

GEN_ATOMIC_HELPER_FN(smin_fetch, MIN, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umin_fetch, MIN,  DATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(smax_fetch, MAX, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umax_fetch, MAX,  DATA_TYPE, new)

#define ADD(X, Y) ((X) + (Y))
#define AND(X, Y) ((X) & (Y))
#define OR(X, Y)  ((X) | (Y))
#define XOR(X, Y) ((X) ^ (Y))

GEN_ATOMIC_HELPER_FN(fetch_add, ADD, DATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(add_fetch, ADD, DATA_TYPE, new)

#define GEN_ATOMIC_FETCH_HELPER(NAME, ATOMIC, OP, RET)               \
ABI_TYPE ATOMIC_NAME(NAME)(CPUArchState *env, abi_ptr addr,          \
                           ABI_TYPE val, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    DATA_TYPE *haddr, raw, old, new;                                \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE, retaddr); \
    raw = qatomic_fetch_##ATOMIC(haddr, BSWAP(val));                 \
    old = BSWAP(raw);                                               \
    new = OP(old, (DATA_TYPE)val);                                  \
    ATOMIC_MMU_CLEANUP;                                             \
    TRACE_RMW(old, new);                                            \
    return RET;                                                     \
}

GEN_ATOMIC_FETCH_HELPER(fetch_and, and, AND, old)
GEN_ATOMIC_FETCH_HELPER(fetch_or, or, OR, old)
GEN_ATOMIC_FETCH_HELPER(fetch_xor, xor, XOR, old)
GEN_ATOMIC_FETCH_HELPER(and_fetch, and, AND, new)
GEN_ATOMIC_FETCH_HELPER(or_fetch, or, OR, new)
GEN_ATOMIC_FETCH_HELPER(xor_fetch, xor, XOR, new)

#undef GEN_ATOMIC_FETCH_HELPER

#undef ADD
#undef AND
#undef OR
#undef XOR

#undef GEN_ATOMIC_HELPER_FN
#endif /* DATA_SIZE < 16 */

#undef END
#endif /* DATA_SIZE > 1 */

#undef BSWAP
#undef TRACE_RMW
#undef TRACE_RMW_SUCCESS
#undef VALUE_EQUAL
#undef VALUE_HIGH
#undef VALUE_LOW
#undef ABI_TYPE
#undef DATA_TYPE
#undef SDATA_TYPE
#undef SUFFIX
#undef DATA_SIZE
#undef SHIFT
