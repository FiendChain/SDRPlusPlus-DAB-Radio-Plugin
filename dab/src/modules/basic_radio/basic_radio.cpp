#include "basic_radio.h"

#include "modules/dab/database/dab_database.h"
#include "modules/dab/database/dab_database_updater.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(__VA_ARGS__)

BasicRadio::BasicRadio(const DAB_Parameters _params, const size_t nb_threads)
: params(_params), fic_runner(_params), thread_pool(nb_threads) 
{
    is_reset_flag = false;
}

BasicRadio::~BasicRadio() = default;

void BasicRadio::Process(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    if (N != params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, params.nb_frame_bits);
        return;
    }

    auto fic_buf = buf.subspan(0,                  params.nb_fic_bits);
    auto msc_buf = buf.subspan(params.nb_fic_bits, params.nb_msc_bits);

    {
        auto lock_channels = std::scoped_lock(mutex_channels);
        if (is_reset_flag) {
            is_reset_flag = false;
            db_manager.ResetAll();
            fic_runner.Reset();
            dab_plus_channels.clear();
        }

        thread_pool.PushTask([this, &fic_buf] {
            fic_runner.Process(fic_buf);
        });

        for (auto& [_, channel]: dab_plus_channels) {
            auto& dab_plus_channel = *(channel.get());
            thread_pool.PushTask([&dab_plus_channel, &msc_buf] {
                dab_plus_channel.Process(msc_buf);
            });
        }

        thread_pool.WaitAll();
    }

    UpdateDatabase();
}

void BasicRadio::Reset() {
    is_reset_flag = true;
}

Basic_DAB_Plus_Channel* BasicRadio::Get_DAB_Plus_Channel(const subchannel_id_t id) {
    auto lock = std::scoped_lock(mutex_channels);
    auto res = dab_plus_channels.find(id);
    if (res == dab_plus_channels.end()) {
        return NULL; 
    }
    return res->second.get();
}

void BasicRadio::UpdateDatabase() {
    auto& misc_info = fic_runner.GetMiscInfo();
    auto& live_db = fic_runner.GetLiveDatabase();
    auto& db_updater = fic_runner.GetDatabaseUpdater();

    db_manager.OnMiscInfo(misc_info);
    const bool is_updated = db_manager.OnDatabaseUpdater(live_db, db_updater);
    if (!is_updated) {
        return;
    }

    auto& db = db_manager.GetDatabase();
    for (auto& subchannel: db.subchannels) {
        AddSubchannel(subchannel.id);
    }
}

bool BasicRadio::AddSubchannel(const subchannel_id_t id) {
    auto res = dab_plus_channels.find(id);
    if (res != dab_plus_channels.end()) {
        return false;
    }

    auto& db = db_manager.GetDatabase();
    auto* subchannel = db.GetSubchannel(id);
    if (subchannel == NULL) {
        LOG_ERROR("Selected subchannel {} which doesn't exist in db", id);
        return false;
    }

    auto* service_component = db.GetServiceComponent_Subchannel(id);
    if (service_component == NULL) {
        LOG_ERROR("Selected subchannel {} has no service component", id);
        return false;
    }

    const auto mode = service_component->transport_mode;
    if (mode != TransportMode::STREAM_MODE_AUDIO) {
        LOG_ERROR("Selected subchannel {} which isn't an audio stream", id);
        return false;
    }

    const auto ascty = service_component->audio_service_type;
    if (ascty != AudioServiceType::DAB_PLUS) {
        LOG_ERROR("Selected subchannel {} isn't a DAB+ stream", id);
        return false;
    }

    // create our instance
    LOG_MESSAGE("Added subchannel {}", id);
    auto lock = std::scoped_lock(mutex_channels);
    res = dab_plus_channels.insert({id, std::make_unique<Basic_DAB_Plus_Channel>(params, *subchannel)}).first;
    obs_dab_plus_channel.Notify(id, *(res->second.get()));
    return true;
}