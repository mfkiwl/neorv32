// #################################################################################################
// # << NEORV32 - Intrinsics + Emulation Functions for the RISC-V "Zfinx" CPU extension >>         #
// # ********************************************************************************************* #
// # The intrinsics provided by this library allow to use the hardware floating-point unit of the  #
// # RISC-V Zfinx CPU extension without the need for Zfinx support by the compiler / toolchain.    #
// # ********************************************************************************************* #
// # BSD 3-Clause License                                                                          #
// #                                                                                               #
// # Copyright (c) 2021, Stephan Nolting. All rights reserved.                                     #
// #                                                                                               #
// # Redistribution and use in source and binary forms, with or without modification, are          #
// # permitted provided that the following conditions are met:                                     #
// #                                                                                               #
// # 1. Redistributions of source code must retain the above copyright notice, this list of        #
// #    conditions and the following disclaimer.                                                   #
// #                                                                                               #
// # 2. Redistributions in binary form must reproduce the above copyright notice, this list of     #
// #    conditions and the following disclaimer in the documentation and/or other materials        #
// #    provided with the distribution.                                                            #
// #                                                                                               #
// # 3. Neither the name of the copyright holder nor the names of its contributors may be used to  #
// #    endorse or promote products derived from this software without specific prior written      #
// #    permission.                                                                                #
// #                                                                                               #
// # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS   #
// # OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF               #
// # MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE    #
// # COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,     #
// # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE #
// # GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED    #
// # AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING     #
// # NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED  #
// # OF THE POSSIBILITY OF SUCH DAMAGE.                                                            #
// # ********************************************************************************************* #
// # The NEORV32 Processor - https://github.com/stnolting/neorv32              (c) Stephan Nolting #
// #################################################################################################


/**********************************************************************//**
 * @file floating_point_test/neorv32_zfinx_extension_intrinsics.h
 * @author Stephan Nolting
 *
 * @brief "Intrinsic" library for the NEORV32 single-precision floating-point in x registers (Zfinx) extension
 * @brief Also provides emulation functions for all intrinsics (functionality re-built in pure software). The functionality of the emulation
 * @brief functions is based on the RISC-V floating-point spec.
 *
 * @note All operations from this library use the default GCC "round to nearest, ties to even" rounding mode.
 *
 * @warning This library is just a temporary fall-back until the Zfinx extensions are supported by the upstream RISC-V GCC port.
 **************************************************************************/
 
#ifndef neorv32_zfinx_extension_intrinsics_h
#define neorv32_zfinx_extension_intrinsics_h

#define __USE_GNU

#include <fenv.h>
//#pragma STDC FENV_ACCESS ON

#define _GNU_SOURCE

#include <float.h>
#include <math.h>


/**********************************************************************//**
 * Sanity check
 **************************************************************************/
#if defined __riscv_f || (__riscv_flen == 32)
  #error Application programs using the Zfinx intrinsic library have to be compiled WITHOUT the <F> MARCH ISA attribute!
#endif


/**********************************************************************//**
 * Custom data type to access floating-point values as native floats and in binary representation
 **************************************************************************/
typedef union
{
  uint32_t binary_value; /**< Access as native float */
  float    float_value;  /**< Access in binary representation */
} float_conv_t;


// ################################################################################################
// Helper functions
// ################################################################################################

/**********************************************************************//**
 * Flush to zero if denormal number.
 *
 * @warning Subnormal numbers are not supported yet! Flush them to zero.
 *
 * @param[in] tmp Source operand.
 * @return Result.
 **************************************************************************/
float subnormal_flush(float tmp) {

  float res = tmp;

  if (fpclassify(tmp) == FP_SUBNORMAL) {
    if (signbit(tmp) != 0) {
      res = -0.0f;
    }
    else {
      res = +0.0f;
    }
  }

  return res;
}


// ################################################################################################
// Exception access
// ################################################################################################

/**********************************************************************//**
 * Get exception flags from fflags CSR (floating-point hardware).
 *
 * @return Floating point exception status word.
 **************************************************************************/
uint32_t get_hw_exceptions(void) {

  uint32_t res = neorv32_cpu_csr_read(CSR_FFLAGS);

  neorv32_cpu_csr_write(CSR_FFLAGS, 0); // clear status word

  return res;
}


/**********************************************************************//**
 * Get exception flags from C runtime (floating-point emulation).
 *
 * @warning WORK-IN-PROGRESS!
 *
 * @return Floating point exception status word.
 **************************************************************************/
uint32_t get_sw_exceptions(void) {

  const uint32_t FP_EXC_NV_C = 1 << 0; // invalid operation
  const uint32_t FP_EXC_DZ_C = 1 << 1; // divide by zero
  const uint32_t FP_EXC_OF_C = 1 << 2; // overflow
  const uint32_t FP_EXC_UF_C = 1 << 3; // underflow
  const uint32_t FP_EXC_NX_C = 1 << 4; // inexact

  int fpeRaised = fetestexcept(FE_ALL_EXCEPT);

  uint32_t res = 0;

  if (fpeRaised & FE_INVALID)   { res |= FP_EXC_NV_C; }
  if (fpeRaised & FE_DIVBYZERO) { res |= FP_EXC_DZ_C; }
  if (fpeRaised & FE_OVERFLOW)  { res |= FP_EXC_OF_C; }
  if (fpeRaised & FE_UNDERFLOW) { res |= FP_EXC_UF_C; }
  if (fpeRaised & FE_INEXACT)   { res |= FP_EXC_NX_C; }

  feclearexcept(FE_ALL_EXCEPT);

  return res;
}


// ################################################################################################
// "Intrinsics"
// ################################################################################################

/**********************************************************************//**
 * Single-precision floating-point addition
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fadds(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fadd.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0000000, a1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point subtraction
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fsubs(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fsub.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0000100, a1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point multiplication
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fmuls(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fmul.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0001000, a1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point minimum
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fmins(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fmin.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0010100, a1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point maximum
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fmaxs(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fmax.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0010100, a1, a0, 0b001, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point convert float to unsigned integer
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline uint32_t __attribute__ ((always_inline)) riscv_intrinsic_fcvt_wus(float rs1) {

  float_conv_t opa;
  opa.float_value = rs1;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a));

  // fcvt.wu.s a0, a0
  CUSTOM_INSTR_R2_TYPE(0b1100000, x1, a0, 0b000, a0, 0b1010011);

  return result;
}


/**********************************************************************//**
 * Single-precision floating-point convert float to signed integer
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline int32_t __attribute__ ((always_inline)) riscv_intrinsic_fcvt_ws(float rs1) {

  float_conv_t opa;
  opa.float_value = rs1;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a));

  // fcvt.w.s a0, a0
  CUSTOM_INSTR_R2_TYPE(0b1100000, x0, a0, 0b000, a0, 0b1010011);

  return (int32_t)result;
}


/**********************************************************************//**
 * Single-precision floating-point convert unsigned integer to float
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fcvt_swu(uint32_t rs1) {

  float_conv_t res;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = rs1;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a));

  // fcvt.s.wu a0, a0
  CUSTOM_INSTR_R2_TYPE(0b1101000, x1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point convert signed integer to float
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fcvt_sw(int32_t rs1) {

  float_conv_t res;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = (uint32_t)rs1;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a));

  // fcvt.s.w a0, a0
  CUSTOM_INSTR_R2_TYPE(0b1101000, x0, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point equal comparison
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline uint32_t __attribute__ ((always_inline)) riscv_intrinsic_feqs(float rs1, float rs2) {

  float_conv_t opa, opb;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // feq.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b1010000, a1, a0, 0b010, a0, 0b1010011);

  return result;
}


/**********************************************************************//**
 * Single-precision floating-point less-than comparison
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline uint32_t __attribute__ ((always_inline)) riscv_intrinsic_flts(float rs1, float rs2) {

  float_conv_t opa, opb;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // flt.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b1010000, a1, a0, 0b001, a0, 0b1010011);

  return result;
}


/**********************************************************************//**
 * Single-precision floating-point less-than-or-equal comparison
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline uint32_t __attribute__ ((always_inline)) riscv_intrinsic_fles(float rs1, float rs2) {

  float_conv_t opa, opb;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fle.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b1010000, a1, a0, 0b000, a0, 0b1010011);

  return result;
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fsgnjs(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fsgnj.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0010000, a1, a0, 0b000, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection NOT
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fsgnjns(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fsgnjn.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0010000, a1, a0, 0b001, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection XOR
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fsgnjxs(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fsgnjx.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0010000, a1, a0, 0b010, a0, 0b1010011);

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point number classification
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline uint32_t __attribute__ ((always_inline)) riscv_intrinsic_fclasss(float rs1) {

  float_conv_t opa;
  opa.float_value = rs1;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("" : [output] "=r" (result) : [input_i] "r" (tmp_a));

  // fclass.s a0, a0
  CUSTOM_INSTR_R2_TYPE(0b1110000, x0, a0, 0b001, a0, 0b1010011);

  return result;
}


// ################################################################################################
// !!! UNSUPPORTED instructions !!!
// ################################################################################################

/**********************************************************************//**
 * Single-precision floating-point division
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @param[in] rs2 Source operand 2 (a1).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fdivs(float rs1, float rs2) {

  float_conv_t opa, opb, res;
  opa.float_value = rs1;
  opb.float_value = rs2;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));

  // fdiv.s a0, a0, x1
  CUSTOM_INSTR_R2_TYPE(0b0001100, a1, a0, 0b000, a0, 0b1010011);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point square root
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0).
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fsqrts(float rs1) {

  float_conv_t opa, res;
  opa.float_value = rs1;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], x0" : : [input_i] "r" (tmp_a));

  // fsqrt.s a0, a0, a1
  CUSTOM_INSTR_R2_TYPE(0b0101100, a1, a0, 0b000, a0, 0b1010011);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point fused multiply-add
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0)
 * @param[in] rs2 Source operand 2 (a1)
 * @param[in] rs3 Source operand 3 (a2)
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fmadds(float rs1, float rs2, float rs3) {

  float_conv_t opa, opb, opc, res;
  opa.float_value = rs1;
  opb.float_value = rs2;
  opc.float_value = rs3;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;
  register uint32_t tmp_c  __asm__ ("a2") = opc.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_b), [input_j] "r" (tmp_c));

  // fmadd.s a0, a0, a1, a2
  CUSTOM_INSTR_R3_TYPE(a2, a1, a0, 0b000, a0, 0b1000011);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point fused multiply-sub
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0)
 * @param[in] rs2 Source operand 2 (a1)
 * @param[in] rs3 Source operand 3 (a2)
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fmsubs(float rs1, float rs2, float rs3) {

  float_conv_t opa, opb, opc, res;
  opa.float_value = rs1;
  opb.float_value = rs2;
  opc.float_value = rs3;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;
  register uint32_t tmp_c  __asm__ ("a2") = opc.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_b), [input_j] "r" (tmp_c));

  // fmsub.s a0, a0, a1, a2
  CUSTOM_INSTR_R3_TYPE(a2, a1, a0, 0b000, a0, 0b1000111);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point fused negated multiply-sub
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0)
 * @param[in] rs2 Source operand 2 (a1)
 * @param[in] rs3 Source operand 3 (a2)
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fnmsubs(float rs1, float rs2, float rs3) {

  float_conv_t opa, opb, opc, res;
  opa.float_value = rs1;
  opb.float_value = rs2;
  opc.float_value = rs3;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;
  register uint32_t tmp_c  __asm__ ("a2") = opc.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_b), [input_j] "r" (tmp_c));

  // fnmsub.s a0, a0, a1, a2
  CUSTOM_INSTR_R3_TYPE(a2, a1, a0, 0b000, a0, 0b1001011);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


/**********************************************************************//**
 * Single-precision floating-point fused negated multiply-add
 *
 * @warning This instruction is not supported and should raise an illegal instruction exception when executed.
 *
 * @param[in] rs1 Source operand 1 (a0)
 * @param[in] rs2 Source operand 2 (a1)
 * @param[in] rs3 Source operand 3 (a2)
 * @return Result.
 **************************************************************************/
inline float __attribute__ ((always_inline)) riscv_intrinsic_fnmadds(float rs1, float rs2, float rs3) {

  float_conv_t opa, opb, opc, res;
  opa.float_value = rs1;
  opb.float_value = rs2;
  opc.float_value = rs3;

  register uint32_t result __asm__ ("a0");
  register uint32_t tmp_a  __asm__ ("a0") = opa.binary_value;
  register uint32_t tmp_b  __asm__ ("a1") = opb.binary_value;
  register uint32_t tmp_c  __asm__ ("a2") = opc.binary_value;

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_a), [input_j] "r" (tmp_b));
  asm volatile ("add x0, %[input_i], %[input_j]" : : [input_i] "r" (tmp_b), [input_j] "r" (tmp_c));

  // fnmadd.s a0, a0, a1, a2
  CUSTOM_INSTR_R3_TYPE(a2, a1, a0, 0b000, a0, 0b1001111);

  // dummy instruction to prevent GCC "constprop" optimization
  asm volatile ("add %[res], %[input], x0" : [res] "=r" (result) : [input] "r" (result) );

  res.binary_value = result;
  return res.float_value;
}


// ################################################################################################
// Emulation functions
// ################################################################################################

/**********************************************************************//**
 * Single-precision floating-point addition
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fadds(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  float res = opa + opb;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point subtraction
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fsubs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  float res = opa - opb;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point multiplication
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fmuls(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  float res = opa * opb;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point minimum
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fmins(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  union {
  uint32_t binary_value; /**< Access as native float */
  float    float_value;  /**< Access in binary representation */
  } tmp_a, tmp_b;

  if ((fpclassify(opa) == FP_NAN) && (fpclassify(opb) == FP_NAN)) {
    return nanf("");
  }

  if (fpclassify(opa) == FP_NAN) {
    return opb;
  }

  if (fpclassify(opb) == FP_NAN) {
    return opa;
  }

  // RISC-V spec: -0 < +0
  tmp_a.float_value = opa;
  tmp_b.float_value = opb;
  if (((tmp_a.binary_value == 0x80000000) && (tmp_b.binary_value == 0x00000000)) ||
      ((tmp_a.binary_value == 0x00000000) && (tmp_b.binary_value == 0x80000000))) {
    return -0.0f;
  }

  return fmin(opa, opb);
}


/**********************************************************************//**
 * Single-precision floating-point maximum
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fmaxs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  union {
  uint32_t binary_value; /**< Access as native float */
  float    float_value;  /**< Access in binary representation */
  } tmp_a, tmp_b;


  if ((fpclassify(opa) == FP_NAN) && (fpclassify(opb) == FP_NAN)) {
    return nanf("");
  }

  if (fpclassify(opa) == FP_NAN) {
    return opb;
  }

  if (fpclassify(opb) == FP_NAN) {
    return opa;
  }

  // RISC-V spec: -0 < +0
  tmp_a.float_value = opa;
  tmp_b.float_value = opb;
  if (((tmp_a.binary_value == 0x80000000) && (tmp_b.binary_value == 0x00000000)) ||
      ((tmp_a.binary_value == 0x00000000) && (tmp_b.binary_value == 0x80000000))) {
    return +0.0f;
  }

  return fmax(opa, opb);
}


/**********************************************************************//**
 * Single-precision floating-point float to unsigned integer
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
uint32_t __attribute__ ((noinline)) riscv_emulate_fcvt_wus(float rs1) {

  float opa = subnormal_flush(rs1);

  return (uint32_t)roundf(opa);
}


/**********************************************************************//**
 * Single-precision floating-point float to signed integer
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
int32_t __attribute__ ((noinline)) riscv_emulate_fcvt_ws(float rs1) {

  float opa = subnormal_flush(rs1);

  return (int32_t)roundf(opa);
}


/**********************************************************************//**
 * Single-precision floating-point unsigned integer to float
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fcvt_swu(uint32_t rs1) {

  return (float)rs1;
}


/**********************************************************************//**
 * Single-precision floating-point signed integer to float
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fcvt_sw(int32_t rs1) {

  return (float)rs1;
}


/**********************************************************************//**
 * Single-precision floating-point equal comparison
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
uint32_t __attribute__ ((noinline)) riscv_emulate_feqs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  if ((fpclassify(opa) == FP_NAN) || (fpclassify(opb) == FP_NAN)) {
    return 0;
  }

  if isless(opa, opb) {
    return 0;
  }
  else if isgreater(opa, opb) {
    return 0;
  }
  else {
    return 1;
  }
}


/**********************************************************************//**
 * Single-precision floating-point less-than comparison
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
uint32_t __attribute__ ((noinline)) riscv_emulate_flts(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  if ((fpclassify(opa) == FP_NAN) || (fpclassify(opb) == FP_NAN)) {
    return 0;
  }

  if isless(opa, opb) {
    return 1;
  }
  else {
    return 0;
  }
}


/**********************************************************************//**
 * Single-precision floating-point less-than-or-equal comparison
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
uint32_t __attribute__ ((noinline)) riscv_emulate_fles(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  if ((fpclassify(opa) == FP_NAN) || (fpclassify(opb) == FP_NAN)) {
    return 0;
  }

  if islessequal(opa, opb) {
    return 1;
  }
  else {
    return 0;
  }
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fsgnjs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  int sign_1 = (int)signbit(opa);
  int sign_2 = (int)signbit(opb);
  float res = 0;

  if (sign_2 != 0) { // opb is negative
    if (sign_1 == 0) {
      res = -opa;
    }
    else {
      res = opa;
    }
  }
  else { // opb is positive
    if (sign_1 == 0) {
      res = opa;
    }
    else {
      res = -opa;
    }
  }

  return res;
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection NOT
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fsgnjns(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  int sign_1 = (int)signbit(opa);
  int sign_2 = (int)signbit(opb);
  float res = 0;

  if (sign_2 != 0) { // opb is negative
    if (sign_1 == 0) {
      res = opa;
    }
    else {
      res = -opa;
    }
  }
  else { // opb is positive
    if (sign_1 == 0) {
      res = -opa;
    }
    else {
      res = opa;
    }
  }

  return res;
}


/**********************************************************************//**
 * Single-precision floating-point sign-injection XOR
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fsgnjxs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  int sign_1 = (int)signbit(opa);
  int sign_2 = (int)signbit(opb);
  float res = 0;

  if (((sign_1 == 0) && (sign_2 != 0)) || ((sign_1 != 0) && (sign_2 == 0))) {
    if (sign_1 == 0) {
      res = -opa;
    }
    else {
      res = opa;
    }
  }
  else {
    if (sign_1 == 0) {
      res = opa;
    }
    else {
      res = -opa;
    }
  }

  return res;
}


/**********************************************************************//**
 * Single-precision floating-point number classification
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
uint32_t __attribute__ ((noinline)) riscv_emulate_fclasss(float rs1) {

  float opa = subnormal_flush(rs1);

  union {
    uint32_t binary_value; /**< Access as native float */
    float    float_value;  /**< Access in binary representation */
  } aux;

  // RISC-V classify result layout
  const uint32_t CLASS_NEG_INF    = 1 << 0; // negative infinity
  const uint32_t CLASS_NEG_NORM   = 1 << 1; // negative normal number
  const uint32_t CLASS_NEG_DENORM = 1 << 2; // negative subnormal number
  const uint32_t CLASS_NEG_ZERO   = 1 << 3; // negative zero
  const uint32_t CLASS_POS_ZERO   = 1 << 4; // positive zero
  const uint32_t CLASS_POS_DENORM = 1 << 5; // positive subnormal number
  const uint32_t CLASS_POS_NORM   = 1 << 6; // positive normal number
  const uint32_t CLASS_POS_INF    = 1 << 7; // positive infinity
  const uint32_t CLASS_SNAN       = 1 << 8; // signaling NaN (sNaN)
  const uint32_t CLASS_QNAN       = 1 << 9; // quiet NaN (qNaN)

  int tmp = fpclassify(opa);
  int sgn = (int)signbit(opa);

  uint32_t res = 0;

  // infinity
  if (tmp == FP_INFINITE) {
    if (sgn) { res |= CLASS_NEG_INF; }
    else     { res |= CLASS_POS_INF; }
  }

  // zero
  if (tmp == FP_ZERO) {
    if (sgn) { res |= CLASS_NEG_ZERO; }
    else     { res |= CLASS_POS_ZERO; }
  }

  // normal
  if (tmp == FP_NORMAL) {
    if (sgn) { res |= CLASS_NEG_NORM; }
    else     { res |= CLASS_POS_NORM; }
  }

  // subnormal
  if (tmp == FP_SUBNORMAL) {
    if (sgn) { res |= CLASS_NEG_DENORM; }
    else     { res |= CLASS_POS_DENORM; }
  }

  // NaN
  if (tmp == FP_NAN) {
    aux.float_value = opa;
    if ((aux.binary_value >> 22) & 0b1) { // bit 22 (mantissa's MSB) is set -> canonical (quiet) NAN
      res |= CLASS_QNAN;
    }
    else {
      res |= CLASS_SNAN;
    }
  }

  return res;
}


/**********************************************************************//**
 * Single-precision floating-point division
 *
 * @param[in] rs1 Source operand 1.
 * @param[in] rs2 Source operand 2.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fdivs(float rs1, float rs2) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);

  float res = opa / opb;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point square root
 *
 * @param[in] rs1 Source operand 1.
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fsqrts(float rs1) {

  float opa = subnormal_flush(rs1);

  float res = sqrtf(opa);
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point fused multiply-add
 *
 * @note "noinline" attributed to make sure arguments/return values are in a0 and a1.
 *
 * @warning This instruction is not supported!
 *
 * @param[in] rs1 Source operand 1
 * @param[in] rs2 Source operand 2
 * @param[in] rs3 Source operand 3
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fmadds(float rs1, float rs2, float rs3) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);
  float opc = subnormal_flush(rs3);

  float res = (opa * opb) + opc;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point fused multiply-sub
 *
 * @param[in] rs1 Source operand 1
 * @param[in] rs2 Source operand 2
 * @param[in] rs3 Source operand 3
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fmsubs(float rs1, float rs2, float rs3) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);
  float opc = subnormal_flush(rs3);

  float res = (opa * opb) - opc;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point fused negated multiply-sub
 *
 * @param[in] rs1 Source operand 1
 * @param[in] rs2 Source operand 2
 * @param[in] rs3 Source operand 3
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fnmsubs(float rs1, float rs2, float rs3) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);
  float opc = subnormal_flush(rs3);

  float res = -(opa * opb) + opc;
  return subnormal_flush(res);
}


/**********************************************************************//**
 * Single-precision floating-point fused negated multiply-add
 *
 * @param[in] rs1 Source operand 1
 * @param[in] rs2 Source operand 2
 * @param[in] rs3 Source operand 3
 * @return Result.
 **************************************************************************/
float __attribute__ ((noinline)) riscv_emulate_fnmadds(float rs1, float rs2, float rs3) {

  float opa = subnormal_flush(rs1);
  float opb = subnormal_flush(rs2);
  float opc = subnormal_flush(rs3);

  float res = -(opa * opb) - opc;
  return subnormal_flush(res);
}


#endif // neorv32_zfinx_extension_intrinsics_h
 