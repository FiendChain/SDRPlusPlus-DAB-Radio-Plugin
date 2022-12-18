#include "dab_module.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <gui/gui.h>
#include <gui/style.h>

#include <module.h>
#include <signal_path/signal_path.h>
#include <dsp/sink.h>
#include <complex>
#include <string>

#include "dab_decoder.h"
#include "audio_player.h"
#include "render_dab_module.h"

ConfigManager config; // extern

DisabledScoped::DisabledScoped(bool _is_disabled)
: is_disabled(_is_disabled) 
{
    if (is_disabled) style::beginDisabled();
}

DisabledScoped::~DisabledScoped() {
    if (is_disabled) style::endDisabled();
}

DAB_Decoder_Sink::DAB_Decoder_Sink(std::shared_ptr<DAB_Decoder> _decoder) 
: decoder(_decoder) {}

DAB_Decoder_Sink::~DAB_Decoder_Sink() {
    if (!base_type::_block_init) return;
    base_type::stop();
}

int DAB_Decoder_Sink::run() {
    int count = base_type::_in->read();
    if (count < 0) { return -1; }

    auto* buf = base_type::_in->readBuf;
    Process(reinterpret_cast<std::complex<float>*>(buf), count);

    base_type::_in->flush();
    return count;
}

void DAB_Decoder_Sink::Process(const std::complex<float>* x, const int N) {
    decoder->Process({ x, (size_t)N });
}

Audio_Player_Stream::Audio_Player_Stream(AudioPlayer& audio_player)
{
    output_stream.clearReadStop();
    output_stream.clearWriteStop();

    audio_player.OnBlock().Attach([this](tcb::span<const Frame<float>> rd_buf) {
        Process(rd_buf);
    });
}

Audio_Player_Stream::~Audio_Player_Stream() {
    output_stream.stopReader();
    output_stream.stopWriter();
};

void Audio_Player_Stream::Process(tcb::span<const Frame<float>> rd_buf) {
    auto wr_buf = output_stream.writeBuf;
    const size_t N = rd_buf.size();
    const float A = 1.0f;
    for (size_t i = 0; i < N; i++) {
        wr_buf[i].l = rd_buf[i].channels[0] * A;
        wr_buf[i].r = rd_buf[i].channels[1] * A;
    }
    output_stream.swap(N);
}


DABModule::DABModule(std::string _name) 
{
    name = _name;
    is_enabled = false;
    vfo = NULL;

    const int TRANSMISSION_MODE = 1;
    dab_decoder = std::make_shared<DAB_Decoder>(TRANSMISSION_MODE);
    decoder_sink = std::make_unique<DAB_Decoder_Sink>(dab_decoder);
    audio_player_stream = std::make_unique<Audio_Player_Stream>(dab_decoder->GetAudioPlayer());

    ev_handler_sample_rate_change.ctx = this;
    ev_handler_sample_rate_change.handler = [](float sample_rate, void* ctx) {
        auto* e = reinterpret_cast<DABModule*>(ctx);
        e->OnSampleRateChange(sample_rate);
    };

    const int Faudio_in = (int)dab_decoder->GetAudioPlayer().GetSampleRate();

    audio_resampler.init(
        &(audio_player_stream->GetOutputStream()),
        Faudio_in, Faudio_in);

    audio_stream.init(
        &audio_resampler.out, 
        &ev_handler_sample_rate_change, 
        Faudio_in);
    audio_stream.setVolume(1.0f);
    sigpath::sinkManager.registerStream(name, &audio_stream);
    audio_stream.start();
    audio_resampler.start();

    gui::menu.registerEntry(name, [](void *ctx) {
        auto* mod = reinterpret_cast<DABModule*>(ctx);
        mod->RenderMenu();
    }, this, this);

    // Load configuration
    config.acquire();
    bool is_modified = false;
    if (!config.conf.contains("is_enabled")) {
        config.conf["is_enabled"] = false;
        is_modified = true;
    }
    const bool cfg_is_enabled = config.conf["is_enabled"];
    config.release(is_modified);
    if (cfg_is_enabled) {
        enable();
    }
}

DABModule::~DABModule() {
    audio_resampler.stop();
    audio_stream.stop();
    if (isEnabled()) {
        decoder_sink->stop();
        sigpath::vfoManager.deleteVFO(vfo);
    }
    sigpath::sinkManager.unregisterStream(name);
}

void DABModule::postInit() {}

void DABModule::enable() { 
    is_enabled = true; 
    if (!vfo) {
        // NOTE: Use the entire 2.048e6 frequency range so that if we have a large
        //       frequency offset the VFO doesn't low pass filter out subcarriers
        const float MIN_BANDWIDTH = 2.048e6f;
        vfo = sigpath::vfoManager.createVFO(
            name, ImGui::WaterfallVFO::REF_CENTER,
            0, MIN_BANDWIDTH, MIN_BANDWIDTH, MIN_BANDWIDTH, MIN_BANDWIDTH, true);
        decoder_sink->setInput(vfo->output);
        decoder_sink->start();
    }

    config.acquire();
    config.conf["is_enabled"] = true;
    config.release(true);
}
void DABModule::disable() { 
    is_enabled = false; 
    if (vfo) {
        decoder_sink->stop();
        sigpath::vfoManager.deleteVFO(vfo);
        vfo = NULL;
    }

    config.acquire();
    config.conf["is_enabled"] = false;
    config.release(true);
}

bool DABModule::isEnabled() { return is_enabled; }

void DABModule::RenderMenu() {
    auto disabled_scope = DisabledScoped(!is_enabled);
    auto& ofdm_demod = dab_decoder->GetOFDMDemodulator();
    auto& basic_radio = dab_decoder->GetBasicRadio();
    auto& mixer = dab_decoder->GetAudioPlayer().GetMixer();
    RenderDABModule(ofdm_demod, basic_radio, mixer);
}

void DABModule::OnSampleRateChange(float new_sample_rate) {
    audio_resampler.setOutSamplerate(new_sample_rate);
}