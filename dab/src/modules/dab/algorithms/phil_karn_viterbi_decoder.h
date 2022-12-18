// Phil Karn's Viterbi decoder implementation
// Source: https://github.com/zleffke/libfec
// SSE2 intrinsics are used to improve the performance by up to 8 times

#pragma once

#include <stdint.h>

struct vitdec_t;

#define CONSTRAINT_LENGTH 7
#define CODE_RATE 4
#define DECISIONTYPE uint64_t
#define DECISIONTYPE_BITSIZE 64 
#define COMPUTETYPE int16_t
// If we want to divide the soft decision error
#define METRICSHIFT 0
#define PRECISIONSHIFT 0
// Constants used
#define RENORMALIZE_THRESHOLD   (SHRT_MAX-3000) // If the error starts to overflow, reduce it to this
#define INITIAL_START_ERROR     SHRT_MIN        // Initial error of initial state
#define INITIAL_NON_START_ERROR (SHRT_MIN+3000) // Initial error of non-initial states

vitdec_t* create_viterbi(
    const uint8_t polys[CODE_RATE], const int len, 
    const COMPUTETYPE soft_decision_high, const COMPUTETYPE soft_decision_low);

void delete_viterbi(vitdec_t* vp);
void init_viterbi(vitdec_t* vp, int starting_state);

// Scalar code: 1x speed
void update_viterbi_blk_scalar(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits);

#if defined(__SSSE3__)
// SSE2 code: 8x speed
void update_viterbi_blk_ssse3(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits);
#endif

#if defined(__AVX2__)
// AVX2 code: 16x speed
void update_viterbi_blk_avx2(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits);
#endif

// Detect the capabilities of the machine and select the best method
static void update_viterbi_blk_auto(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits) {
    #if defined(__AVX2__)
    #pragma message("Compiling viterbi decoder with AVX2")
    update_viterbi_blk_avx2(vp, syms, nbits);
    #elif defined(__SSSE3__)
    #pragma message("Compiling viterbi decoder with SSSE3")
    update_viterbi_blk_ssse3(vp, syms, nbits);
    #else
    #pragma message("Compiling viterbi decoder with scalar instructions")
    update_viterbi_blk_scalar(vp, syms, nbits);
    #endif
}

/* Viterbi chainback */
void chainback_viterbi(
    vitdec_t* const vp,
    unsigned char* data,            /* Decoded output data */
    const unsigned int nbits,       /* Number of data bits */
    const unsigned int endstate);   /* Terminal encoder state */

COMPUTETYPE get_error_viterbi(vitdec_t* vp, const int state);