#include "ofdm_modulator.h"
#include <fftw3.h>
#include <algorithm>

OFDM_Modulator::OFDM_Modulator(
    const OFDM_Params _params, 
    tcb::span<const std::complex<float>> _prs_fft_ref)
:   params(_params),
    frame_out_size(_params.nb_null_period + _params.nb_symbol_period*_params.nb_frame_symbols),
    data_in_size((_params.nb_frame_symbols-1)*_params.nb_data_carriers*2/8)
{
    ifft_plan = fftwf_plan_dft_1d((int)params.nb_fft, NULL, NULL, FFTW_BACKWARD, FFTW_ESTIMATE);

    // interleave the bits for a OFDM symbol containing N data carriers
    prs_fft_ref.resize(params.nb_fft);
    prs_time_ref.resize(params.nb_symbol_period);

    std::copy_n(_prs_fft_ref.begin(), params.nb_fft, prs_fft_ref.begin());

    // create our time domain prs symbol with the cyclic prefix
    {
        auto buf = tcb::span(prs_time_ref).subspan(params.nb_cyclic_prefix, params.nb_fft);
        CalculateIFFT(prs_fft_ref, buf);
        for (int i = 0; i < params.nb_cyclic_prefix; i++) {
            prs_time_ref[i] = prs_time_ref[params.nb_fft+i];
        }
    }

    last_sym_fft.resize(params.nb_fft);
    curr_sym_fft.resize(params.nb_fft);

    for (int i = 0; i < params.nb_fft; i++) {
        curr_sym_fft[i] = std::complex<float>(0,0);
        last_sym_fft[i] = std::complex<float>(0,0);
    }
}

OFDM_Modulator::~OFDM_Modulator() 
{
    fftwf_destroy_plan(ifft_plan);
}

bool OFDM_Modulator::ProcessBlock(
    tcb::span<std::complex<float>> frame_out_buf, 
    tcb::span<const uint8_t> data_in_buf)
{
    const size_t nb_frame_out = frame_out_buf.size();
    const size_t nb_data_in = data_in_buf.size();
    // invalid buffer sizes
    if (nb_data_in != data_in_size) {
        return false;
    }
    if (nb_frame_out != frame_out_size) {
        return false;
    }

    // null period
    for (int i = 0; i < params.nb_null_period; i++) {
        frame_out_buf[i] = std::complex<float>(0,0);
    }

    // prs symbol
    for (int i = 0; i < params.nb_symbol_period; i++) {
        frame_out_buf[i+params.nb_null_period] = prs_time_ref[i];
    }

    // seed the dqpsk fft buffers
    for (int i = 0; i < params.nb_fft; i++) {
        last_sym_fft[i] = prs_fft_ref[i];
    }

    // data symbols
    {
        const size_t nb_data_symbols = params.nb_frame_symbols-1;
        const size_t nb_data_in = params.nb_data_carriers*2/8; 
        const size_t nb_out = params.nb_symbol_period;
        // account for the PRS (phase reference symbol)
        auto* data_symbols = &frame_out_buf[params.nb_null_period + params.nb_symbol_period];

        for (int i = 0; i < nb_data_symbols; i++) {
            const auto* sym_data_in = &data_in_buf[i*nb_data_in];
            auto* sym_out = &data_symbols[i*params.nb_symbol_period];
            CreateDataSymbol(
                {sym_data_in, nb_data_in}, 
                {sym_out, params.nb_symbol_period});
        }
    }

    return true;
}

void OFDM_Modulator::CreateDataSymbol(
    tcb::span<const uint8_t> sym_data_in, 
    tcb::span<std::complex<float>> sym_out)
{
    const size_t nb_data_in = sym_data_in.size(); 
    const size_t nb_out = sym_out.size();

    static float A = 1.0f/std::sqrt(2.0f);
    static std::complex<float> PHASE_MAP[4] = {
        {-A,-A}, {A,-A}, {A,A}, {-A,A}};

    // Create raw fft bins
    {
        // create fft for -F/2 <= f < 0
        size_t curr_carrier = params.nb_fft - params.nb_data_carriers/2;
        for (int i = 0; i < nb_data_in/2; i++) {
            const uint8_t b = sym_data_in[i];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }

        // create fft for 0 < f <= F/2 
        curr_carrier = 1;
        for (int i = 0; i < nb_data_in/2; i++) {
            const size_t j = nb_data_in/2 + i;
            const uint8_t b = sym_data_in[j];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }
    }


    // get the dqpsk
    // arg(z0*z1) = arg(z0) + arg(z1)
    {
        for (int i = 0; i < params.nb_data_carriers/2; i++) {
            const size_t j = params.nb_fft - params.nb_data_carriers/2 + i;
            curr_sym_fft[j] = last_sym_fft[j] * curr_sym_fft[j];
        }

        for (int i = 0; i < params.nb_data_carriers/2; i++) {
            const size_t j = 1+i;
            curr_sym_fft[j] = last_sym_fft[j] * curr_sym_fft[j];
        }
    }

    // get ifft of symbol
    {
        auto buf = sym_out.subspan(params.nb_cyclic_prefix, params.nb_fft);
        CalculateIFFT(curr_sym_fft, buf);
    }

    // create cyclic prefix
    for (int i = 0; i < params.nb_cyclic_prefix; i++) {
        sym_out[i] = sym_out[i+params.nb_fft];
    }

    // swap fft buffers
    std::swap(last_sym_fft, curr_sym_fft);
}

void OFDM_Modulator::CalculateIFFT(
    tcb::span<const std::complex<float>> fft_in, 
    tcb::span<std::complex<float>> fft_out)
{
    fftwf_execute_dft(
        ifft_plan, 
        (fftwf_complex*)fft_in.data(),
        (fftwf_complex*)fft_out.data());
}