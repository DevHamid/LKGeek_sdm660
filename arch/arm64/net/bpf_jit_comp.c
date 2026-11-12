/*
 * BPF JIT compiler for ARM64
 *
 * Copyright (C) 2014-2016 Zi Shen Lim <zlim.lnx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "bpf_jit: " fmt

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/set_memory.h>

#include "bpf_jit.h"

#define TMP_REG_1 (MAX_BPF_JIT_REG + 0)
#define TMP_REG_2 (MAX_BPF_JIT_REG + 1)
#define TCALL_CNT (MAX_BPF_JIT_REG + 2)
#define TMP_REG_3 (MAX_BPF_JIT_REG + 3)

/* Map BPF registers to A64 registers */
static const int bpf2a64[] = {
	/* return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = A64_R(7),
	/* arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = A64_R(0),
	[BPF_REG_2] = A64_R(1),
	[BPF_REG_3] = A64_R(2),
	[BPF_REG_4] = A64_R(3),
	[BPF_REG_5] = A64_R(4),
	/* callee saved registers that in-kernel function will preserve */
	[BPF_REG_6] = A64_R(19),
	[BPF_REG_7] = A64_R(20),
	[BPF_REG_8] = A64_R(21),
	[BPF_REG_9] = A64_R(22),
	/* read-only frame pointer to access stack */
	[BPF_REG_FP] = A64_R(25),
	/* temporary registers for internal BPF JIT */
	[TMP_REG_1] = A64_R(10),
	[TMP_REG_2] = A64_R(11),
	[TMP_REG_3] = A64_R(12),
	/* tail_call_cnt */
	[TCALL_CNT] = A64_R(26),
	/* temporary register for blinding constants */
	[BPF_REG_AX] = A64_R(9),
};

struct jit_ctx {
	const struct bpf_prog *prog;
	int idx;
	int epilogue_offset;
	int *offset;
	__le32 *image;
	u32 stack_size;
	unsigned long *tb;
};

static inline void emit(const u32 insn, struct jit_ctx *ctx)
{
	if (ctx->image != NULL)
		ctx->image[ctx->idx] = cpu_to_le32(insn);

	ctx->idx++;
}

static inline void emit_a64_mov_i(const int is64, const int reg,
				      const s32 val, struct jit_ctx *ctx)
{
	u16 hi = val >> 16;
	u16 lo = val & 0xffff;

	if (hi == 0xffff) {
		emit(A64_MOVN(is64, reg, (u16)~lo, 0), ctx);
		return;
	}
	if (!hi) {
		emit(A64_MOVZ(is64, reg, lo, 0), ctx);
		return;
	}
	if (!lo && (!is64 || !(hi & 0x8000))) {
		emit(A64_MOVZ(is64, reg, hi, 16), ctx);
		return;
	}
	if (lo == 0xffff && (!is64 || (hi & 0x8000))) {
		emit(A64_MOVN(is64, reg, (u16)~hi, 16), ctx);
		return;
	}
	if (hi & 0x8000) {
		emit(A64_MOVN(is64, reg, (u16)~hi, 16), ctx);
		emit(A64_MOVK(is64, reg, lo, 0), ctx);
	} else {
		emit(A64_MOVZ(is64, reg, lo, 0), ctx);
		emit(A64_MOVK(is64, reg, hi, 16), ctx);
	}
}

static int i64_i16_blocks(const u64 val, bool inverse)
{
	return (((val >>  0) & 0xffff) != (inverse ? 0xffff : 0x0000)) +
	       (((val >> 16) & 0xffff) != (inverse ? 0xffff : 0x0000)) +
	       (((val >> 32) & 0xffff) != (inverse ? 0xffff : 0x0000)) +
	       (((val >> 48) & 0xffff) != (inverse ? 0xffff : 0x0000));
}

static inline void emit_a64_mov_i64(const int reg, const u64 val,
				    struct jit_ctx *ctx)
{
	u64 nrm_tmp = val, rev_tmp = ~val;
	bool inverse;
	int shift;

	if (!(nrm_tmp >> 32))
		return emit_a64_mov_i(0, reg, (u32)val, ctx);

	inverse = i64_i16_blocks(nrm_tmp, true) < i64_i16_blocks(nrm_tmp, false);
	shift = max(round_down((inverse ? (fls64(rev_tmp) - 1) :
					  (fls64(nrm_tmp) - 1)), 16), 0);
	if (inverse)
		emit(A64_MOVN(1, reg, (rev_tmp >> shift) & 0xffff, shift), ctx);
	else
		emit(A64_MOVZ(1, reg, (nrm_tmp >> shift) & 0xffff, shift), ctx);
	shift -= 16;
	while (shift >= 0) {
		if (((nrm_tmp >> shift) & 0xffff) != (inverse ? 0xffff : 0x0000))
			emit(A64_MOVK(1, reg, (nrm_tmp >> shift) & 0xffff, shift), ctx);
		shift -= 16;
	}
}

/*
 * This is an unoptimized 64 immediate emission used for BPF to BPF call
 * addresses. It will always do a full 64 bit decomposition as otherwise
 * more complexity in the last extra pass is required since we previously
 * reserved 4 instructions for the address.
 */
static inline void emit_addr_mov_i64(const int reg, const u64 val,
				     struct jit_ctx *ctx)
{
	u64 tmp = val;
	int shift = 0;

	emit(A64_MOVZ(1, reg, tmp & 0xffff, shift), ctx);
	for (;shift < 48;) {
		tmp >>= 16;
		shift += 16;
		emit(A64_MOVK(1, reg, tmp & 0xffff, shift), ctx);
	}
}

/*
 * Helpers for emitting BPF loads and stores.
 *
 * A BPF memory access has an access size and a signed 32-bit offset. arm64
 * can encode it three ways: a scaled unsigned 12-bit immediate offset
 * (LDR/STR), an unscaled signed 9-bit byte offset (LDUR/STUR), or a register
 * offset (LDR/STR with Xm). Pick the cheapest form that fits.
 */
static bool is_lsi_offset(int offset, int scale)
{
	if (offset < 0)
		return false;

	if (!scale)
		return offset <= 4095;

	return offset <= 4095 << scale;
}

/* The unscaled forms take a signed 9-bit byte offset instead */
static bool is_lsu_offset(int offset)
{
	return offset >= -256 && offset < 256;
}

/* The A64 size of a BPF load or store, and the scale of its offset */
static enum aarch64_insn_size_type a64_ldst_size(const u8 code, int *scale)
{
	switch (BPF_SIZE(code)) {
	case BPF_W:
		*scale = 2;
		return AARCH64_INSN_SIZE_32;
	case BPF_H:
		*scale = 1;
		return AARCH64_INSN_SIZE_16;
	case BPF_B:
		*scale = 0;
		return AARCH64_INSN_SIZE_8;
	case BPF_DW:
		*scale = 3;
		return AARCH64_INSN_SIZE_64;
	default:
		*scale = 0;
		return AARCH64_INSN_SIZE_8;
	}
}

/* The forms of a load or store: scaled immediate, unscaled, register offset */
enum a64_ldst_form { A64_LDST_IMM, A64_LDST_UNSCALED, A64_LDST_REG };

/* The A64 type of a BPF load or store in the given form */
static enum aarch64_insn_ldst_type a64_ldst_type(const u8 code,
						 enum a64_ldst_form form)
{
	static const enum aarch64_insn_ldst_type types[][2] = {
		[A64_LDST_IMM] = {
			AARCH64_INSN_LDST_LOAD_IMM_OFFSET,
			AARCH64_INSN_LDST_STORE_IMM_OFFSET,
		},
		[A64_LDST_UNSCALED] = {
			AARCH64_INSN_LDST_LOAD_UNSCALED_IMM_OFFSET,
			AARCH64_INSN_LDST_STORE_UNSCALED_IMM_OFFSET,
		},
		[A64_LDST_REG] = {
			AARCH64_INSN_LDST_LOAD_REG_OFFSET,
			AARCH64_INSN_LDST_STORE_REG_OFFSET,
		},
	};

	if (BPF_CLASS(code) != BPF_LDX)
		return types[form][1];
	return types[form][0];
}

/* Emit a BPF load or store of the given access size at the given offset */
static void emit_ldst(const u8 code, const u8 rt, const u8 rn, const s32 off,
		      const u8 tmp, struct jit_ctx *ctx)
{
	enum aarch64_insn_size_type size;
	enum aarch64_insn_ldst_type type;
	int scale;

	size = a64_ldst_size(code, &scale);

	if (is_lsi_offset(off, scale)) {
		type = a64_ldst_type(code, A64_LDST_IMM);
		emit(aarch64_insn_gen_load_store_imm(rt, rn, off, size, type),
		     ctx);
	} else if (is_lsu_offset(off)) {
		type = a64_ldst_type(code, A64_LDST_UNSCALED);
		emit(aarch64_insn_gen_load_store_imm(rt, rn, off, size, type),
		     ctx);
	} else {
		type = a64_ldst_type(code, A64_LDST_REG);
		emit_a64_mov_i(1, tmp, off, ctx);
		emit(aarch64_insn_gen_load_store_reg(rt, rn, tmp, size, type),
		     ctx);
	}
}

static inline int bpf2a64_offset(int bpf_to, int bpf_from,
				 const struct jit_ctx *ctx)
{
	int to = ctx->offset[bpf_to];
	/* -1 to account for the Branch instruction */
	int from = ctx->offset[bpf_from] - 1;

	return to - from;
}

static void jit_fill_hole(void *area, unsigned int size)
{
	__le32 *ptr;
	/* We are guaranteed to have aligned memory. */
	for (ptr = area; size >= sizeof(u32); size -= sizeof(u32))
		*ptr++ = cpu_to_le32(AARCH64_BREAK_FAULT);
}

static inline int epilogue_offset(const struct jit_ctx *ctx)
{
	int to = ctx->epilogue_offset;
	int from = ctx->idx;

	return to - from;
}

/* Stack must be multiples of 16B */
#define STACK_ALIGN(sz) (((sz) + 15) & ~15)

/* Tail call offset to jump into */
#define PROLOGUE_OFFSET 7

static int build_prologue(struct jit_ctx *ctx, bool ebpf_from_cbpf)
{
	const struct bpf_prog *prog = ctx->prog;
	const u8 r6 = bpf2a64[BPF_REG_6];
	const u8 r7 = bpf2a64[BPF_REG_7];
	const u8 r8 = bpf2a64[BPF_REG_8];
	const u8 r9 = bpf2a64[BPF_REG_9];
	const u8 fp = bpf2a64[BPF_REG_FP];
	const u8 tcc = bpf2a64[TCALL_CNT];
	const int idx0 = ctx->idx;
	int cur_offset;

	/*
	 * BPF prog stack layout
	 *
	 *                         high
	 * original A64_SP =>   0:+-----+ BPF prologue
	 *                        |FP/LR|
	 * current A64_FP =>  -16:+-----+
	 *                        | ... | callee saved registers
	 * BPF fp register => -64:+-----+ <= (BPF_FP)
	 *                        |     |
	 *                        | ... | BPF prog stack
	 *                        |     |
	 *                        +-----+ <= (BPF_FP - prog->aux->stack_depth)
	 *                        |RSVD | padding
	 * current A64_SP =>      +-----+ <= (BPF_FP - ctx->stack_size)
	 *                        |     |
	 *                        | ... | Function call stack
	 *                        |     |
	 *                        +-----+
	 *                          low
	 *
	 */

	/* Save FP and LR registers to stay align with ARM64 AAPCS */
	emit(A64_PUSH(A64_FP, A64_LR, A64_SP), ctx);
	emit(A64_MOV(1, A64_FP, A64_SP), ctx);

	/* Save callee-saved registers */
	emit(A64_PUSH(r6, r7, A64_SP), ctx);
	emit(A64_PUSH(r8, r9, A64_SP), ctx);
	emit(A64_PUSH(fp, tcc, A64_SP), ctx);

	/* Set up BPF prog stack base register */
	emit(A64_MOV(1, fp, A64_SP), ctx);

	if (!ebpf_from_cbpf) {
		/* Initialize tail_call_cnt */
		emit(A64_MOVZ(1, tcc, 0, 0), ctx);

		cur_offset = ctx->idx - idx0;
		if (cur_offset != PROLOGUE_OFFSET) {
			pr_err_once("PROLOGUE_OFFSET = %d, expected %d!\n",
				    cur_offset, PROLOGUE_OFFSET);
			return -1;
		}
	}

	ctx->stack_size = STACK_ALIGN(prog->aux->stack_depth);

	/* Set up function call stack */
	emit(A64_SUB_I(1, A64_SP, A64_SP, ctx->stack_size), ctx);
	return 0;
}

static int out_offset = -1; /* initialized on the first pass of build_body() */
static int emit_bpf_tail_call(struct jit_ctx *ctx)
{
	/* bpf_tail_call(void *prog_ctx, struct bpf_array *array, u64 index) */
	const u8 r2 = bpf2a64[BPF_REG_2];
	const u8 r3 = bpf2a64[BPF_REG_3];

	const u8 tmp = bpf2a64[TMP_REG_1];
	const u8 prg = bpf2a64[TMP_REG_2];
	const u8 tcc = bpf2a64[TCALL_CNT];
	const int idx0 = ctx->idx;
#define cur_offset (ctx->idx - idx0)
#define jmp_offset (out_offset - (cur_offset))
	size_t off;

	/* The struct offsets fit the immediate forms, the count fits cmp */
	BUILD_BUG_ON(offsetof(struct bpf_array, map.max_entries) % 4 ||
		     offsetof(struct bpf_array, map.max_entries) > 0xfff * 4);
	BUILD_BUG_ON(offsetof(struct bpf_array, ptrs) > 0xfff);
	BUILD_BUG_ON(offsetof(struct bpf_prog, bpf_func) % 8 ||
		     offsetof(struct bpf_prog, bpf_func) > 0xfff * 8);
	BUILD_BUG_ON(MAX_TAIL_CALL_CNT > 0xfff);

	/* if (index >= array->map.max_entries)
	off = offsetof(struct bpf_array, map.max_entries);
	emit_a64_mov_i64(tmp, off, ctx);
	emit(A64_LDR32(tmp, r2, tmp), ctx);
	emit(A64_MOV(0, r3, r3), ctx);
	emit(A64_CMP(0, r3, tmp), ctx);
	emit(A64_B_(A64_COND_CS, jmp_offset), ctx);

	/* if (tail_call_cnt > MAX_TAIL_CALL_CNT)
	 *     goto out;
	 * tail_call_cnt++;
	 */
	emit(A64_CMP_I(1, tcc, MAX_TAIL_CALL_CNT), ctx);
	emit(A64_B_(A64_COND_HI, jmp_offset), ctx);
	emit(A64_ADD_I(1, tcc, tcc, 1), ctx);

	/* prog = array->ptrs[index];
	 * if (prog == NULL)
	 *     goto out;
	 */
	off = offsetof(struct bpf_array, ptrs);
	emit_a64_mov_i64(tmp, off, ctx);
	emit(A64_ADD(1, tmp, r2, tmp), ctx);
	emit(A64_LSL(1, prg, r3, 3), ctx);
	emit(A64_LDR64(prg, tmp, prg), ctx);
	emit(A64_CBZ(1, prg, jmp_offset), ctx);

	/* goto *(prog->bpf_func + prologue_offset); */
	off = offsetof(struct bpf_prog, bpf_func);
	emit_a64_mov_i64(tmp, off, ctx);
	emit(A64_LDR64(tmp, prg, tmp), ctx);
	emit(A64_ADD_I(1, tmp, tmp, sizeof(u32) * PROLOGUE_OFFSET), ctx);
	emit(A64_ADD_I(1, A64_SP, A64_SP, ctx->stack_size), ctx);
	emit(A64_BR(tmp), ctx);

	/* out: */
	if (out_offset == -1)
		out_offset = cur_offset;
	if (cur_offset != out_offset) {
		pr_err_once("tail_call out_offset = %d, expected %d!\n",
			    cur_offset, out_offset);
		return -1;
	}
	return 0;
#undef cur_offset
#undef jmp_offset
}

static void build_epilogue(struct jit_ctx *ctx)
{
	const u8 r0 = bpf2a64[BPF_REG_0];
	const u8 r6 = bpf2a64[BPF_REG_6];
	const u8 r7 = bpf2a64[BPF_REG_7];
	const u8 r8 = bpf2a64[BPF_REG_8];
	const u8 r9 = bpf2a64[BPF_REG_9];
	const u8 fp = bpf2a64[BPF_REG_FP];

	/* We're done with BPF stack */
	emit(A64_ADD_I(1, A64_SP, A64_SP, ctx->stack_size), ctx);

	/* Restore fs (x25) and x26 */
	emit(A64_POP(fp, A64_R(26), A64_SP), ctx);

	/* Restore callee-saved register */
	emit(A64_POP(r8, r9, A64_SP), ctx);
	emit(A64_POP(r6, r7, A64_SP), ctx);

	/* Restore FP/LR registers */
	emit(A64_POP(A64_FP, A64_LR, A64_SP), ctx);

	/* Set return value */
	emit(A64_MOV(1, A64_R(0), r0), ctx);

	emit(A64_RET(A64_LR), ctx);
}

/* JITs an eBPF instruction.
 * Returns:
 * 0  - successfully JITed an 8-byte eBPF instruction.
 * >0 - successfully JITed a 16-byte eBPF instruction.
 * <0 - failed to JIT.
 */

static bool emit_mul_const(bool is64, u8 dst, u8 tmp, s32 imm, struct jit_ctx *ctx)
{
	u64 c = imm < 0 ? -(u64)(s64)imm : imm;
	int tz, n;

	switch (imm) {
	case 0:
		emit_a64_mov_i(is64, dst, 0, ctx);
		return true;
	case 1:
		if (!is64)
			emit(A64_MOV(0, dst, dst), ctx);
		return true;
	case -1:
		emit(A64_NEG(is64, dst, dst), ctx);
		return true;
	}
	tz = __ffs(c);
	c >>= tz;
	if (c == 1) {
		if (imm > 0)
			emit(A64_LSL(is64, dst, dst, tz), ctx);
		else
			emit(A64_SUB_LSL(is64, dst, A64_ZR, dst, tz), ctx);
		return true;
	}
	if (imm > 0) {
		if (is_power_of_2(c - 1)) {
			n = ilog2(c - 1);
			if (n > 4 && tz)
				return false;
			emit(A64_ADD_LSL(is64, dst, dst, dst, n), ctx);
			if (tz)
				emit(A64_LSL(is64, dst, dst, tz), ctx);
			return true;
		}
		if (is_power_of_2(c + 1)) {
			n = ilog2(c + 1);
			if (tz > 4)
				return false;
			emit(A64_LSL(is64, tmp, dst, n + tz), ctx);
			emit(A64_SUB_LSL(is64, dst, tmp, dst, tz), ctx);
			return true;
		}
	} else {
		if (is_power_of_2(c + 1)) {
			n = ilog2(c + 1);
			if (!tz) {
				emit(A64_SUB_LSL(is64, dst, dst, dst, n), ctx);
				return true;
			}
			if (n + tz > 4)
				return false;
			emit(A64_LSL(is64, tmp, dst, tz), ctx);
			emit(A64_SUB_LSL(is64, dst, tmp, dst, n + tz), ctx);
			return true;
		}
		if (!tz && is_power_of_2(c - 1)) {
			n = ilog2(c - 1);
			if (n > 4)
				return false;
			emit(A64_ADD_LSL(is64, dst, dst, dst, n), ctx);
			emit(A64_NEG(is64, dst, dst), ctx);
			return true;
		}
	}
	return false;
}

/*
 * The bit that a sign test, an unsigned 32-bit compare at the sign bit or a
 * single bit test checks, with *nonzero set if it branches on a set bit, or -1.
 *
 * The TBZ/TBNZ encoders take the branch target as a signed 14-bit field of
 * 4-byte units, i.e. -8192..8191 instructions.  When the target falls outside
 * that window the caller must fall back to the compare-and-conditional-branch
 * form, so pass the target in and refuse here rather than emitting a branch
 * the hardware cannot encode.
 */
static int tb_bit(u8 op, bool is64, s32 imm, int jmp_offset, bool *nonzero)
{
	const int sign = is64 ? 63 : 31;
	u64 mask;

	/*
	 * Outside the TBZ/TBNZ reach; use A64_CMP_I/A64_CMN_I + A64_B_ instead,
	 * which covers +/-128 KiB and is emitted by the caller's fallback path.
	 */
	if (jmp_offset < -8192 || jmp_offset > 8191)
		return -1;

	switch (op) {
	case BPF_JSLT:
	case BPF_JSGE:
		/* x < 0, x >= 0 */
		if (imm)
			return -1;
		*nonzero = op == BPF_JSLT;
		return sign;
	case BPF_JSGT:
	case BPF_JSLE:
		/* x > -1, x <= -1 */
		if (imm != -1)
			return -1;
		*nonzero = op == BPF_JSLE;
		return sign;
	case BPF_JGT:
	case BPF_JLE:
		/* x > 0x7fffffff, x <= 0x7fffffff in 32 bits */
		if (is64 || imm != 0x7fffffff)
			return -1;
		*nonzero = op == BPF_JGT;
		return 31;
	case BPF_JGE:
	case BPF_JLT:
		/* x >= 0x80000000, x < 0x80000000 in 32 bits */
		if (is64 || imm != INT_MIN)
			return -1;
		*nonzero = op == BPF_JGE;
		return 31;
	case BPF_JSET:
		mask = is64 ? (u64)(s64)imm : (u32)imm;
		if (!is_power_of_2(mask))
			return -1;
		*nonzero = true;
		return ilog2(mask);
	default:
		return -1;
	}
}

static int build_insn(const struct bpf_insn *insn, struct jit_ctx *ctx)
{
	const u8 code = insn->code;
	const u8 dst = bpf2a64[insn->dst_reg];
	const u8 src = bpf2a64[insn->src_reg];
	const u8 tmp = bpf2a64[TMP_REG_1];
	const u8 tmp2 = bpf2a64[TMP_REG_2];
	const u8 tmp3 = bpf2a64[TMP_REG_3];
	const s16 off = insn->off;
	const s32 imm = insn->imm;
	const int i = insn - ctx->prog->insnsi;
	const bool is64 = BPF_CLASS(code) == BPF_ALU64 ||
			  BPF_CLASS(code) == BPF_JMP;
	const bool isdw = BPF_SIZE(code) == BPF_DW;
	u8 jmp_op;
	u8 jmp_cond, reg;
	s32 cmp_imm = imm;
	s32 jmp_offset;
	int bit;
	bool nonzero = false;

#define check_imm(bits, imm) do {				\
	if ((((imm) > 0) && ((imm) >> (bits))) ||		\
	    (((imm) < 0) && (~(imm) >> (bits)))) {		\
		pr_info("[%2d] imm=%d(0x%x) out of range\n",	\
			i, imm, imm);				\
		return -EINVAL;					\
	}							\
} while (0)
#define check_imm19(imm) check_imm(19, imm)
#define check_imm26(imm) check_imm(26, imm)

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
	case BPF_ALU64 | BPF_MOV | BPF_X:
		emit(A64_MOV(is64, dst, src), ctx);
		break;
	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit(A64_ADD(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit(A64_SUB(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit(A64_AND(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit(A64_ORR(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit(A64_EOR(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit(A64_MUL(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		switch (BPF_OP(code)) {
		case BPF_DIV:
			emit(A64_UDIV(is64, dst, dst, src), ctx);
			break;
		case BPF_MOD:
			emit(A64_UDIV(is64, tmp, dst, src), ctx);
			emit(A64_MSUB(is64, dst, dst, tmp, src), ctx);
			break;
		}
		break;
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit(A64_LSLV(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(A64_LSRV(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit(A64_ASRV(is64, dst, dst, src), ctx);
		break;
	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit(A64_NEG(is64, dst, dst), ctx);
		break;
	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
	case BPF_ALU | BPF_END | BPF_FROM_BE:
#ifdef CONFIG_CPU_BIG_ENDIAN
		if (BPF_SRC(code) == BPF_FROM_BE)
			goto emit_bswap_uxt;
#else /* !CONFIG_CPU_BIG_ENDIAN */
		if (BPF_SRC(code) == BPF_FROM_LE)
			goto emit_bswap_uxt;
#endif
		switch (imm) {
		case 16:
			emit(A64_REV16(is64, dst, dst), ctx);
			/* zero-extend 16 bits into 64 bits */
			emit(A64_UXTH(is64, dst, dst), ctx);
			break;
		case 32:
			emit(A64_REV32(is64, dst, dst), ctx);
			/* upper 32 bits already cleared */
			break;
		case 64:
			emit(A64_REV64(dst, dst), ctx);
			break;
		}
		break;
emit_bswap_uxt:
		switch (imm) {
		case 16:
			/* zero-extend 16 bits into 64 bits */
			emit(A64_UXTH(is64, dst, dst), ctx);
			break;
		case 32:
			/* zero-extend 32 bits into 64 bits */
			emit(A64_UXTW(is64, dst, dst), ctx);
			break;
		case 64:
			/* nop */
			break;
		}
		break;
	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_a64_mov_i(is64, dst, imm, ctx);
		break;
	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_ADD(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_SUB(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		/* All ones only zero extends the 32-bit variant */
		if (imm == -1) {
			if (!is64)
				emit(A64_MOV(0, dst, dst), ctx);
			break;
		}
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_AND(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		/* The or with all ones is the move of -1 */
		if (imm == -1) {
			emit(A64_MOVN(is64, dst, 0, 0), ctx);
			break;
		}
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_ORR(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		/* The xor with all ones is the not */
		if (imm == -1) {
			emit(A64_MVN(is64, dst, dst), ctx);
			break;
		}
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_EOR(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		if (!emit_mul_const(is64, dst, tmp, imm, ctx)) {
			emit_a64_mov_i(is64, tmp, imm, ctx);
			emit(A64_MUL(is64, dst, dst, tmp), ctx);
		}
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_UDIV(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_a64_mov_i(is64, tmp2, imm, ctx);
		emit(A64_UDIV(is64, tmp, dst, tmp2), ctx);
		emit(A64_MSUB(is64, dst, dst, tmp, tmp2), ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit(A64_LSL(is64, dst, dst, imm), ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		emit(A64_LSR(is64, dst, dst, imm), ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit(A64_ASR(is64, dst, dst, imm), ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		check_imm26(jmp_offset);
		emit(A64_B(jmp_offset), ctx);
		break;
	/* IF (dst COND src) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
		emit(A64_CMP(1, dst, src), ctx);
		jmp_op = BPF_OP(code);
emit_cond_jmp:
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		check_imm19(jmp_offset);
		switch (jmp_op) {
		case BPF_JEQ:
			jmp_cond = A64_COND_EQ;
			break;
		case BPF_JGT:
			jmp_cond = A64_COND_HI;
			break;
		case BPF_JLT:
			jmp_cond = A64_COND_CC;
			break;
		case BPF_JGE:
			jmp_cond = A64_COND_CS;
			break;
		case BPF_JLE:
			jmp_cond = A64_COND_LS;
			break;
		case BPF_JSET:
		case BPF_JNE:
			jmp_cond = A64_COND_NE;
			break;
		case BPF_JSGT:
			jmp_cond = A64_COND_GT;
			break;
		case BPF_JSLT:
			jmp_cond = A64_COND_LT;
			break;
		case BPF_JSGE:
			jmp_cond = A64_COND_GE;
			break;
		case BPF_JSLE:
			jmp_cond = A64_COND_LE;
			break;
		default:
			return -EFAULT;
		}
		emit(A64_B_(jmp_cond, jmp_offset), ctx);
		break;
	case BPF_JMP | BPF_JSET | BPF_X:
		emit(A64_TST(1, dst, src), ctx);
		goto emit_cond_jmp;
	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
		jmp_op = BPF_OP(code);
		/*
		 * A compare against a single bit can be a test-bit-and-branch
		 * instead of a compare plus a conditional branch, which is one
		 * instruction shorter and needs no general purpose register.
		 * The branch target has to be computed first: if it is outside
		 * the TBZ/TBNZ reach the fallback below is used instead.
		 */
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		bit = tb_bit(jmp_op, is64, cmp_imm, jmp_offset, &nonzero);
		if (bit >= 0) {
			emit(nonzero ? A64_TBNZ(dst, bit, jmp_offset) :
			       A64_TBZ(dst, bit, jmp_offset), ctx);
			break;
		}
		/*
		 * The arm64 compare immediates are only 12 bits, so the
		 * hardware cannot compare against every s32 the BPF program
		 * may use. Materialise the immediate when it does not fit.
		 */
		if (is_addsub_imm(cmp_imm)) {
			emit(A64_CMP_I(is64, dst, cmp_imm), ctx);
		} else if (is_addsub_imm(-(u32)cmp_imm)) {
			emit(A64_CMN_I(is64, dst, -cmp_imm), ctx);
		} else {
			emit_a64_mov_i(is64, tmp, cmp_imm, ctx);
			emit(A64_CMP(is64, dst, tmp), ctx);
		}
		goto emit_cond_jmp;
	case BPF_JMP | BPF_JSET | BPF_K:
		/*
		 * A test of a single bit is a test-bit-and-branch, no need to
		 * materialise the immediate or set the flags.
		 */
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		bit = tb_bit(BPF_JSET, is64, imm, jmp_offset, &nonzero);
		if (bit >= 0) {
			jmp_op = BPF_OP(code);
			emit(A64_TBNZ(dst, bit, jmp_offset), ctx);
			break;
		}
		emit_a64_mov_i(1, tmp, imm, ctx);
		emit(A64_TST(1, dst, tmp), ctx);
		goto emit_cond_jmp;
	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		const u8 r0 = bpf2a64[BPF_REG_0];
		const u64 func = (u64)__bpf_call_base + imm;

		if (ctx->prog->is_func)
			emit_addr_mov_i64(tmp, func, ctx);
		else
			emit_a64_mov_i64(tmp, func, ctx);
		emit(A64_BLR(tmp), ctx);
		emit(A64_MOV(1, r0, A64_R(0)), ctx);
		break;
	}
	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_bpf_tail_call(ctx))
			return -EFAULT;
		break;
	/* function return */
	case BPF_JMP | BPF_EXIT:
		/* Optimization: when last instruction is EXIT,
		   simply fallthrough to epilogue. */
		if (i == ctx->prog->len - 1)
			break;
		jmp_offset = epilogue_offset(ctx);
		check_imm26(jmp_offset);
		emit(A64_B(jmp_offset), ctx);
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		const struct bpf_insn insn1 = insn[1];
		u64 imm64;

		imm64 = (u64)insn1.imm << 32 | (u32)imm;
		emit_a64_mov_i64(dst, imm64, ctx);

		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW:
		emit_ldst(code, dst, src, off, tmp, ctx);
		break;

	/* speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		/*
		 * Nothing required here.
		 *
		 * In case of arm64, we rely on the firmware mitigation of
		 * Speculative Store Bypass as controlled via the ssbd kernel
		 * parameter. Whenever the mitigation is enabled, it works
		 * for all of the kernel code with no need to provide any
		 * additional instructions.
		 */
		break;

	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW:
		/* Load imm to a register then store it */
		emit_a64_mov_i(1, tmp2, imm, ctx);
		/*
		 * A zero comes from the zero register, any other imm through a
		 * register, so hand emit_ldst the register holding the value.
		 */
		emit_ldst(code, imm ? tmp2 : A64_ZR, dst, off, tmp, ctx);
		break;

	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW:
		emit_ldst(code, src, dst, off, tmp, ctx);
		break;

	/* STX XADD: lock *(u32 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_W:
	/* STX XADD: lock *(u64 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_DW:
		if (!off) {
			reg = dst;
		} else {
			emit_a64_mov_i(1, tmp, off, ctx);
			emit(A64_ADD(1, tmp, tmp, dst), ctx);
			reg = tmp;
		}
		if (cpus_have_cap(ARM64_HAS_LSE_ATOMICS)) {
			emit(A64_STADD(isdw, reg, src), ctx);
		} else {
			emit(A64_LDXR(isdw, tmp2, reg), ctx);
			emit(A64_ADD(isdw, tmp2, tmp2, src), ctx);
			emit(A64_STXR(isdw, tmp2, reg, tmp3), ctx);
			jmp_offset = -3;
			check_imm19(jmp_offset);
			emit(A64_CBNZ(0, tmp3, jmp_offset), ctx);
		}
		break;

	default:
		pr_err_once("unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

static int build_body(struct jit_ctx *ctx)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		ret = build_insn(insn, ctx);
		if (ret > 0) {
			i++;
			if (ctx->image == NULL)
				ctx->offset[i] = ctx->idx;
			continue;
		}
		if (ctx->image == NULL)
			ctx->offset[i] = ctx->idx;
		if (ret)
			return ret;
	}

	return 0;
}

static int validate_code(struct jit_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->idx; i++) {
		u32 a64_insn = le32_to_cpu(ctx->image[i]);

		if (a64_insn == AARCH64_BREAK_FAULT)
			return -1;
	}

	return 0;
}

static inline void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

struct arm64_jit_data {
	struct bpf_binary_header *header;
	u8 *image;
	struct jit_ctx ctx;
};

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_prog *tmp, *orig_prog = prog;
	struct bpf_binary_header *header;
	struct arm64_jit_data *jit_data;
	bool was_classic = bpf_prog_was_classic(prog);
	bool tmp_blinded = false;
	bool extra_pass = false;
	struct jit_ctx ctx;
	int image_size;
	u8 *image_ptr;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	/* If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter.
	 */
	if (IS_ERR(tmp))
		return orig_prog;
	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}
	if (jit_data->ctx.offset) {
		ctx = jit_data->ctx;
		image_ptr = jit_data->image;
		header = jit_data->header;
		extra_pass = true;
		image_size = sizeof(u32) * ctx.idx;
		goto skip_init_ctx;
	}
	memset(&ctx, 0, sizeof(ctx));
	ctx.prog = prog;

	ctx.offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (ctx.offset == NULL) {
		prog = orig_prog;
		goto out_off;
	}

	/* 1. Initial fake pass to compute ctx->idx. */

	/* Fake pass to fill in ctx->offset. */
	if (build_body(&ctx)) {
		prog = orig_prog;
		goto out_off;
	}

	if (build_prologue(&ctx, was_classic)) {
		prog = orig_prog;
		goto out_off;
	}

	ctx.epilogue_offset = ctx.idx;
	build_epilogue(&ctx);

	/* Now we know the actual image size. */
	image_size = sizeof(u32) * ctx.idx;
	header = bpf_jit_binary_alloc(image_size, &image_ptr,
				      sizeof(u32), jit_fill_hole);
	if (header == NULL) {
		prog = orig_prog;
		goto out_off;
	}

	/* 2. Now, the actual pass. */

	ctx.image = (__le32 *)image_ptr;
skip_init_ctx:
	ctx.idx = 0;

	build_prologue(&ctx, was_classic);

	if (build_body(&ctx)) {
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_off;
	}

	build_epilogue(&ctx);

	/* 3. Extra pass to validate JITed code. */
	if (validate_code(&ctx)) {
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_off;
	}

	/* And we're done. */
	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, 2, ctx.image);

	bpf_flush_icache(header, ctx.image + ctx.idx);

	if (!prog->is_func || extra_pass) {
		if (extra_pass && ctx.idx != jit_data->ctx.idx) {
			pr_err_once("multi-func JIT bug %d != %d\n",
				    ctx.idx, jit_data->ctx.idx);
			bpf_jit_binary_free(header);
			prog->bpf_func = NULL;
			prog->jited = 0;
			prog->jited_len = 0;
			goto out_off;
		}
		bpf_jit_binary_lock_ro(header);
	} else {
		jit_data->ctx = ctx;
		jit_data->image = image_ptr;
		jit_data->header = header;
	}
	prog->bpf_func = (void *)ctx.image;
	prog->jited = 1;
	prog->jited_len = image_size;

	if (!prog->is_func || extra_pass) {
out_off:
		kfree(ctx.offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	return prog;
}

void *bpf_jit_alloc_exec(unsigned long size)
{
	return __vmalloc_node_range(size, PAGE_SIZE, BPF_JIT_REGION_START,
				    BPF_JIT_REGION_END, GFP_KERNEL,
				    PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

void bpf_jit_free_exec(void *addr)
{
	return vfree(addr);
}

#ifdef CONFIG_CFI_CLANG
bool arch_bpf_jit_check_func(const struct bpf_prog *prog)
{
	const uintptr_t func = (const uintptr_t)prog->bpf_func;

	/*
	 * bpf_func must be correctly aligned and within the correct region.
	 */
	if (unlikely(!IS_ALIGNED(func, sizeof(u32))))
		return false;

	return (func >= BPF_JIT_REGION_START && func < BPF_JIT_REGION_END);
}
#endif
