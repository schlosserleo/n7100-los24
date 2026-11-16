#include "RadioServices.h"

namespace android::hardware::radio::n7100 {

/* libsec-ril's RIL_AppStatus has five extra ints (from LineageOS' libsecril-shim) */
struct RIL_AppStatus_samsung {
    RIL_AppType app_type;
    RIL_AppState app_state;
    RIL_PersoSubstate perso_substate;
    char* aid_ptr;
    char* app_label_ptr;
    int pin1_replaced;
    RIL_PinState pin1;
    RIL_PinState pin2;
    int foo[5];
};

struct RIL_CardStatus_v5_samsung {
    RIL_CardState card_state;
    RIL_PinState universal_pin_state;
    int gsm_umts_subscription_app_index;
    int cdma_subscription_app_index;
    int num_applications;
    RIL_AppStatus_samsung applications[RIL_CARD_MAX_APPS];
};

template <class App>
static asim::AppStatus toApp(const App& a) {
    return {.appType = a.app_type,
            .appState = a.app_state,
            .persoSubstate = static_cast<asim::PersoSubstate>(a.perso_substate),
            .aidPtr = str(a.aid_ptr),
            .appLabelPtr = str(a.app_label_ptr),
            .pin1Replaced = a.pin1_replaced != 0,
            .pin1 = static_cast<asim::PinState>(a.pin1),
            .pin2 = static_cast<asim::PinState>(a.pin2)};
}

static asim::CardStatus toCardStatus(void* r, size_t len) {
    asim::CardStatus cs{};
    cs.slotMap = {.physicalSlotId = 0, .portId = 0};
    auto fill = [&cs](const auto* c, int imsIndex) {
        cs.cardState = c->card_state;
        cs.universalPinState = static_cast<asim::PinState>(c->universal_pin_state);
        cs.gsmUmtsSubscriptionAppIndex = c->gsm_umts_subscription_app_index;
        cs.cdmaSubscriptionAppIndex = c->cdma_subscription_app_index;
        cs.imsSubscriptionAppIndex = imsIndex;
        int n = std::min(c->num_applications, RIL_CARD_MAX_APPS);
        for (int i = 0; i < n; i++) cs.applications.push_back(toApp(c->applications[i]));
    };
    if (len == sizeof(RIL_CardStatus_v5_samsung)) {
        fill(static_cast<RIL_CardStatus_v5_samsung*>(r), -1);
    } else if (len >= sizeof(RIL_CardStatus_v6)) {
        auto* c = static_cast<RIL_CardStatus_v6*>(r);
        fill(c, c->ims_subscription_app_index);
    }
    return cs;
}

// EF_ICCID (2FE2 under the MF): ten bytes of BCD with swapped nibbles, padded with F
static std::string iccidFromEf(const char* hex) {
    std::string out;
    if (!hex) return out;
    size_t n = strlen(hex) & ~static_cast<size_t>(1);
    for (size_t i = 0; i < n; i += 2) {
        for (char c : {hex[i + 1], hex[i]}) {
            if (c < '0' || c > '9') return out;
            out.push_back(c);
        }
    }
    return out;
}

// The legacy card status has no ICCID. Android's telephony (subscriptions) takes it from the
// AIDL CardStatus and creates no subscription without it, so read EF_ICCID once per card.
ScopedAStatus RadioSim::getIccCardStatus(int32_t serial) {
    ril(RIL_REQUEST_GET_SIM_STATUS, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        asim::CardStatus cs{};
        if (e == RIL_E_SUCCESS && r) cs = toCardStatus(r, len);
        bool present = cs.cardState == RIL_CARDSTATE_PRESENT;
        bool changed;
        {
            std::lock_guard lk(state().lock);
            changed = state().cardState != cs.cardState;
            state().cardState = cs.cardState;
            if (!present) state().iccid.clear();
            cs.iccid = state().iccid;
        }
        if (!present || !cs.iccid.empty()) {
            if (mResp) mResp->getIccCardStatusResponse(rsp(serial, e), cs);
            if (changed) notifySlotStatus();
            return;
        }
        auto p = std::make_unique<Payload>();
        auto* s = p->make<RIL_SIM_IO_v6>();
        s->command = 0xb0;  // READ BINARY
        s->fileid = 0x2fe2;
        s->path = p->keep("3F00");
        s->p3 = 10;
        ril(RIL_REQUEST_SIM_IO, std::move(p),
            [this, serial, e, cs](RIL_Errno e2, void* r2, size_t len2) mutable {
                std::string iccid;
                if (e2 == RIL_E_SUCCESS && r2 && len2 >= sizeof(RIL_SIM_IO_Response)) {
                    auto* rr = static_cast<RIL_SIM_IO_Response*>(r2);
                    if (rr->sw1 == 0x90 || rr->sw1 == 0x91) iccid = iccidFromEf(rr->simResponse);
                }
                LOG(INFO) << "EF_ICCID: e=" << e2 << ", " << iccid.size() << " digits";
                {
                    std::lock_guard lk(state().lock);
                    state().iccid = iccid;
                }
                cs.iccid = iccid;
                if (mResp) mResp->getIccCardStatusResponse(rsp(serial, e), cs);
                notifySlotStatus();
            });
    });
    return ScopedAStatus::ok();
}

void RadioSim::pinRequest(int32_t serial, int req, std::vector<std::optional<std::string>> args,
                          PinResponse response) {
    ril(req, Payload::strings(args), [this, serial, response](RIL_Errno e, void* r, size_t len) {
        if (mResp) ((*mResp).*response)(rsp(serial, e), intAt(r, len, 0, -1));
    });
}

static std::optional<std::string> aidOrNull(const std::string& aid) {
    return aid.empty() ? std::nullopt : std::optional(aid);
}

ScopedAStatus RadioSim::supplyIccPinForApp(int32_t serial, const std::string& pin,
                                           const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_ENTER_SIM_PIN, {pin, aidOrNull(aid)},
               &asim::IRadioSimResponse::supplyIccPinForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPukForApp(int32_t serial, const std::string& puk,
                                           const std::string& pin, const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_ENTER_SIM_PUK, {puk, pin, aidOrNull(aid)},
               &asim::IRadioSimResponse::supplyIccPukForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPin2ForApp(int32_t serial, const std::string& pin2,
                                            const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_ENTER_SIM_PIN2, {pin2, aidOrNull(aid)},
               &asim::IRadioSimResponse::supplyIccPin2ForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPuk2ForApp(int32_t serial, const std::string& puk2,
                                            const std::string& pin2, const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_ENTER_SIM_PUK2, {puk2, pin2, aidOrNull(aid)},
               &asim::IRadioSimResponse::supplyIccPuk2ForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::changeIccPinForApp(int32_t serial, const std::string& oldPin,
                                           const std::string& newPin, const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_CHANGE_SIM_PIN, {oldPin, newPin, aidOrNull(aid)},
               &asim::IRadioSimResponse::changeIccPinForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::changeIccPin2ForApp(int32_t serial, const std::string& oldPin2,
                                            const std::string& newPin2, const std::string& aid) {
    pinRequest(serial, RIL_REQUEST_CHANGE_SIM_PIN2, {oldPin2, newPin2, aidOrNull(aid)},
               &asim::IRadioSimResponse::changeIccPin2ForAppResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getImsiForApp(int32_t serial, const std::string& aid) {
    ril(RIL_REQUEST_GET_IMSI, Payload::strings({aidOrNull(aid)}),
        [this, serial](RIL_Errno e, void* r, size_t) {
            if (mResp) mResp->getImsiForAppResponse(rsp(serial, e), str(static_cast<char*>(r)));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccIoForApp(int32_t serial, const asim::IccIo& io) {
    auto p = std::make_unique<Payload>();
    auto* s = p->make<RIL_SIM_IO_v6>();
    s->command = io.command;
    s->fileid = io.fileId;
    s->path = p->keepOrNull(io.path);
    s->p1 = io.p1;
    s->p2 = io.p2;
    s->p3 = io.p3;
    s->data = p->keepOrNull(io.data);
    s->pin2 = p->keepOrNull(io.pin2);
    s->aidPtr = p->keepOrNull(io.aid);
    ril(RIL_REQUEST_SIM_IO, std::move(p), [this, serial](RIL_Errno e, void* r, size_t len) {
        asim::IccIoResult res{};
        if (r && len >= sizeof(RIL_SIM_IO_Response)) {
            auto* rr = static_cast<RIL_SIM_IO_Response*>(r);
            res = {.sw1 = rr->sw1, .sw2 = rr->sw2, .simResponse = str(rr->simResponse)};
        }
        if (mResp) mResp->iccIoForAppResponse(rsp(serial, e), res);
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getFacilityLockForApp(int32_t serial, const std::string& facility,
                                              const std::string& password, int32_t serviceClass,
                                              const std::string& appId) {
    ril(RIL_REQUEST_QUERY_FACILITY_LOCK,
        Payload::strings({facility, password, std::to_string(serviceClass), aidOrNull(appId)}),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp) mResp->getFacilityLockForAppResponse(rsp(serial, e), intAt(r, len, 0));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setFacilityLockForApp(int32_t serial, const std::string& facility,
                                              bool lockState, const std::string& password,
                                              int32_t serviceClass, const std::string& appId) {
    ril(RIL_REQUEST_SET_FACILITY_LOCK,
        Payload::strings({facility, lockState ? "1" : "0", password, std::to_string(serviceClass),
                          aidOrNull(appId)}),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp) mResp->setFacilityLockForAppResponse(rsp(serial, e), intAt(r, len, 0, -1));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::reportStkServiceIsRunning(int32_t serial) {
    ril(RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING, Payload::none(),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->reportStkServiceIsRunningResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::sendEnvelope(int32_t serial, const std::string& contents) {
    ril(RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND, Payload::string(contents),
        [this, serial](RIL_Errno e, void* r, size_t) {
            if (mResp) mResp->sendEnvelopeResponse(rsp(serial, e), str(static_cast<char*>(r)));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::sendTerminalResponseToSim(int32_t serial, const std::string& contents) {
    ril(RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE, Payload::string(contents),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->sendTerminalResponseToSimResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::areUiccApplicationsEnabled(int32_t serial) {
    if (mResp) mResp->areUiccApplicationsEnabledResponse(ok(serial), true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setSimCardPower(int32_t serial, asim::CardPowerState powerUp) {
    if (mResp)
        mResp->setSimCardPowerResponse(powerUp == asim::CardPowerState::POWER_UP
                                               ? ok(serial)
                                               : notSupported(serial));
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
