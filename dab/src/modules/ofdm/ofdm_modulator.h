#include <stdint.h>
#include <complex>
#include <vector>
#include "ofdm_params.h"
#include "utility/span.h"

typedef struct fftwf_plan_s* fftwf_plan;                                      \

// simulate a OFDM transmitter using one of the DAB transmission modes
// this will have a sampling rate of 2.048MHz
class OFDM_Modulator 
{
private:
    fftwf_plan ifft_plan;
    const OFDM_Params params;

    const size_t frame_out_size;
    const size_t data_in_size;

    std::vector<std::complex<float>> prs_fft_ref;
    std::vector<std::complex<float>> prs_time_ref;

    // frequency domain buffers used for dqpsk encoding
    std::vector<std::complex<float>> last_sym_fft;
    std::vector<std::complex<float>> curr_sym_fft;
public:
    OFDM_Modulator(
        const OFDM_Params _params, 
        tcb::span<const std::complex<float>> _prs_fft_ref);
    ~OFDM_Modulator();
    bool ProcessBlock(
        tcb::span<std::complex<float>> frame_out_buf, 
        tcb::span<const uint8_t> data_in_buf);
private:
    void CreateDataSymbol(
        tcb::span<const uint8_t> sym_data_in, 
        tcb::span<std::complex<float>> sym_out);
    void CalculateIFFT(
        tcb::span<const std::complex<float>> fft_in, 
        tcb::span<std::complex<float>> fft_out);
};