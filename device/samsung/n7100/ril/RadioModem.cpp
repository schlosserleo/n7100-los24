#include "RadioServices.h"

namespace android::hardware::radio::n7100 {

RilState& state() {
    static RilState s;
    return s;
}

Services& services() {
    static Services s;
    return s;
}

// libsec-ril still reports the pre-v7 radio states (SIM_NOT_READY, SIM_READY, ...): all "on"
static amodem::RadioState toRadioState(int legacy) {
    if (legacy == RADIO_STATE_OFF) return amodem::RadioState::OFF;
    if (legacy == RADIO_STATE_UNAVAILABLE) return amodem::RadioState::UNAVAILABLE;
    return amodem::RadioState::ON;
}

void RadioModem::onResponseFunctionsSet() {
    if (!mInd) return;
    mInd->rilConnected(kUnsol);
    mInd->radioStateChanged(kUnsol, toRadioState(n7100ril::radioState()));
}

ScopedAStatus RadioModem::enableModem(int32_t serial, bool) {
    if (mResp) mResp->enableModemResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getModemStackStatus(int32_t serial) {
    if (mResp) mResp->getModemStackStatusResponse(ok(serial), true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getBasebandVersion(int32_t serial) {
    ril(RIL_REQUEST_BASEBAND_VERSION, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t) {
        if (mResp) mResp->getBasebandVersionResponse(rsp(serial, e), str(static_cast<char*>(r)));
    });
    return ScopedAStatus::ok();
}

// libsec-ril has no DEVICE_IDENTITY; the deprecated GET_IMEI/GET_IMEISV still work
void RadioModem::getImeiAndSv(std::function<void(RIL_Errno, std::string, std::string)> done) {
    ril(RIL_REQUEST_GET_IMEI, Payload::none(), [done](RIL_Errno e, void* r, size_t) {
        std::string imei = str(static_cast<char*>(r));
        if (e != RIL_E_SUCCESS) {
            done(e, "", "");
            return;
        }
        ril(RIL_REQUEST_GET_IMEISV, Payload::none(), [done, imei](RIL_Errno e2, void* r2, size_t) {
            done(RIL_E_SUCCESS, imei, e2 == RIL_E_SUCCESS ? str(static_cast<char*>(r2)) : "");
        });
    });
}

ScopedAStatus RadioModem::getImei(int32_t serial) {
    getImeiAndSv([this, serial](RIL_Errno e, std::string imei, std::string sv) {
        amodem::ImeiInfo info{.type = amodem::ImeiInfo::ImeiType::PRIMARY, .imei = imei, .svn = sv};
        if (mResp) mResp->getImeiResponse(rsp(serial, e), e == RIL_E_SUCCESS ? std::optional(info)
                                                                            : std::nullopt);
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getDeviceIdentity(int32_t serial) {
    getImeiAndSv([this, serial](RIL_Errno e, std::string imei, std::string sv) {
        if (mResp) mResp->getDeviceIdentityResponse(rsp(serial, e), imei, sv, "", "");
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getModemActivityInfo(int32_t serial) {
    amodem::ActivityStatsInfo stats{};
    stats.techSpecificInfo.resize(1);
    stats.techSpecificInfo[0].txmModetimeMs.resize(5);
    if (mResp) mResp->getModemActivityInfoResponse(ok(serial), stats);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getRadioCapability(int32_t serial) {
    using RAF = aidl_radio::RadioAccessFamily;
    amodem::RadioCapability rc{
            .session = 0,
            .phase = amodem::RadioCapability::PHASE_CONFIGURED,
            .raf = static_cast<int32_t>(RAF::GSM) | static_cast<int32_t>(RAF::GPRS) |
                   static_cast<int32_t>(RAF::EDGE) | static_cast<int32_t>(RAF::UMTS) |
                   static_cast<int32_t>(RAF::HSDPA) | static_cast<int32_t>(RAF::HSUPA) |
                   static_cast<int32_t>(RAF::HSPA) | static_cast<int32_t>(RAF::HSPAP),
            .logicalModemUuid = "",
            .status = amodem::RadioCapability::STATUS_NONE,
    };
    if (mResp) mResp->getRadioCapabilityResponse(ok(serial), rc);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::requestShutdown(int32_t serial) {
    ril(RIL_REQUEST_RADIO_POWER, Payload::ints({0}), [this, serial](RIL_Errno e, void*, size_t) {
        if (mResp) mResp->requestShutdownResponse(rsp(serial, e));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::sendDeviceState(int32_t serial, amodem::DeviceStateType, bool) {
    if (mResp) mResp->sendDeviceStateResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::setRadioPower(int32_t serial, bool on, bool, bool) {
    ril(RIL_REQUEST_RADIO_POWER, Payload::ints({on ? 1 : 0}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setRadioPowerResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

/* --- config --- */

ScopedAStatus RadioConfig::getHalDeviceCapabilities(int32_t serial) {
    if (mResp) mResp->getHalDeviceCapabilitiesResponse(ok(serial), true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getNumOfLiveModems(int32_t serial) {
    if (mResp) mResp->getNumOfLiveModemsResponse(ok(serial), 1);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getPhoneCapability(int32_t serial) {
    aconfig::PhoneCapability cap{.maxActiveData = 1,
                                 .maxActiveInternetData = 1,
                                 .isInternetLingeringSupported = false,
                                 .logicalModemIds = {0},
                                 .maxActiveVoice = 1};
    if (mResp) mResp->getPhoneCapabilityResponse(ok(serial), cap);
    return ScopedAStatus::ok();
}

aconfig::SimSlotStatus currentSlotStatus() {
    aconfig::SimSlotStatus slot{};
    std::lock_guard lk(state().lock);
    slot.cardState = state().cardState;
    slot.portInfo = {{.iccId = state().iccid, .logicalSlotId = 0, .portActive = true}};
    return slot;
}

void notifySlotStatus() {
    auto& sv = services();
    if (sv.config && sv.config->ind())
        sv.config->ind()->simSlotsStatusChanged(kUnsol, {currentSlotStatus()});
}

// The framework asks this before its first GET_SIM_STATUS: ask the RIL instead of answering
// "absent" from a state that has not been filled in yet.
ScopedAStatus RadioConfig::getSimSlotsStatus(int32_t serial) {
    ril(RIL_REQUEST_GET_SIM_STATUS, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (e == RIL_E_SUCCESS && r && len >= sizeof(int)) {
            // card_state is the first member of every RIL_CardStatus layout
            std::lock_guard lk(state().lock);
            state().cardState = *static_cast<int*>(r);
            if (state().cardState != RIL_CARDSTATE_PRESENT) state().iccid.clear();
        }
        if (mResp) mResp->getSimSlotsStatusResponse(ok(serial), {currentSlotStatus()});
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setNumOfLiveModems(int32_t serial, int8_t) {
    if (mResp) mResp->setNumOfLiveModemsResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setPreferredDataModem(int32_t serial, int8_t) {
    if (mResp) mResp->setPreferredDataModemResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setSimSlotsMapping(int32_t serial,
                                              const std::vector<aconfig::SlotPortMapping>&) {
    if (mResp) mResp->setSimSlotsMappingResponse(ok(serial));
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
