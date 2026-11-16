#include "RadioServices.h"

#include <utils/SystemClock.h>

namespace android::hardware::radio::n7100 {

static bool isSimState(int s) {
    return s >= RADIO_STATE_SIM_NOT_READY && s <= RADIO_STATE_NV_READY;
}

static void radioStateChanged() {
    auto& sv = services();
    int legacy = n7100ril::radioState();
    int old;
    {
        std::lock_guard lk(state().lock);
        old = state().legacyRadioState;
        state().legacyRadioState = legacy;
    }
    auto radio = legacy == RADIO_STATE_OFF           ? amodem::RadioState::OFF
                 : legacy == RADIO_STATE_UNAVAILABLE ? amodem::RadioState::UNAVAILABLE
                                                     : amodem::RadioState::ON;
    if (sv.modem && sv.modem->ind()) sv.modem->ind()->radioStateChanged(kUnsol, radio);
    // pre-v7 RILs signal SIM state changes through the radio state (shim)
    if (isSimState(legacy) && legacy != old && sv.sim && sv.sim->ind())
        sv.sim->ind()->simStatusChanged(kUnsol);
    if (isSimState(legacy) && !isSimState(old) && sv.network && sv.network->ind())
        sv.network->ind()->voiceRadioTechChanged(kUnsol, aidl_radio::RadioTechnology::UMTS);
}

void onUnsolicited(int id, const void* d, size_t len) {
    auto& sv = services();
    void* data = const_cast<void*>(d);
    switch (id) {
        case RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED:
            radioStateChanged();
            break;
        case RIL_UNSOL_RIL_CONNECTED:
            if (sv.modem && sv.modem->ind()) sv.modem->ind()->rilConnected(kUnsol);
            break;
        case RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED:
            if (sv.voice && sv.voice->ind()) sv.voice->ind()->callStateChanged(kUnsol);
            break;
        case RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED:
            if (sv.network && sv.network->ind()) sv.network->ind()->networkStateChanged(kUnsol);
            break;
        case RIL_UNSOL_RESPONSE_NEW_SMS:
            if (sv.messaging && sv.messaging->ind())
                sv.messaging->ind()->newSms(kUnsol, hexToBytes(static_cast<char*>(data)));
            break;
        case RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT:
            if (sv.messaging && sv.messaging->ind())
                sv.messaging->ind()->newSmsStatusReport(kUnsol, hexToBytes(static_cast<char*>(data)));
            break;
        case RIL_UNSOL_RESPONSE_NEW_SMS_ON_SIM:
            if (sv.messaging && sv.messaging->ind())
                sv.messaging->ind()->newSmsOnSim(kUnsol, intAt(data, len, 0));
            break;
        case RIL_UNSOL_SIM_SMS_STORAGE_FULL:
            if (sv.messaging && sv.messaging->ind()) sv.messaging->ind()->simSmsStorageFull(kUnsol);
            break;
        case RIL_UNSOL_ON_USSD: {
            auto s = stringsOf(data, len);
            s.resize(2);
            if (sv.voice && sv.voice->ind())
                sv.voice->ind()->onUssd(kUnsol, static_cast<avoice::UssdModeType>(atoiOr(s[0], 0)),
                                        s[1]);
            break;
        }
        case RIL_UNSOL_NITZ_TIME_RECEIVED:
            if (sv.network && sv.network->ind())
                sv.network->ind()->nitzTimeReceived(kUnsol, str(static_cast<char*>(data)),
                                                    android::elapsedRealtime(), 0);
            break;
        case RIL_UNSOL_SIGNAL_STRENGTH:
            if (sv.network && sv.network->ind())
                sv.network->ind()->currentSignalStrength(kUnsol,
                                                         RadioNetwork::toSignalStrength(data, len));
            break;
        case RIL_UNSOL_DATA_CALL_LIST_CHANGED:
            if (sv.data && sv.data->ind())
                sv.data->ind()->dataCallListChanged(kUnsol, RadioData::toDataCallList(data, len));
            break;
        case RIL_UNSOL_SUPP_SVC_NOTIFICATION:
            if (data && len >= sizeof(RIL_SuppSvcNotification) && sv.network && sv.network->ind()) {
                auto* n = static_cast<RIL_SuppSvcNotification*>(data);
                sv.network->ind()->suppSvcNotify(kUnsol, {.isMT = n->notificationType == 1,
                                                          .code = n->code,
                                                          .index = n->index,
                                                          .type = n->type,
                                                          .number = str(n->number)});
            }
            break;
        case RIL_UNSOL_STK_SESSION_END:
            if (sv.sim && sv.sim->ind()) sv.sim->ind()->stkSessionEnd(kUnsol);
            break;
        case RIL_UNSOL_STK_PROACTIVE_COMMAND:
            if (sv.sim && sv.sim->ind())
                sv.sim->ind()->stkProactiveCommand(kUnsol, str(static_cast<char*>(data)));
            break;
        case RIL_UNSOL_STK_EVENT_NOTIFY:
            if (sv.sim && sv.sim->ind())
                sv.sim->ind()->stkEventNotify(kUnsol, str(static_cast<char*>(data)));
            break;
        case RIL_UNSOL_STK_CALL_SETUP:
            if (sv.voice && sv.voice->ind())
                sv.voice->ind()->stkCallSetup(kUnsol, intAt(data, len, 0));
            break;
        case RIL_UNSOL_SIM_REFRESH:
            if (data && len >= sizeof(RIL_SimRefreshResponse_v7) && sv.sim && sv.sim->ind()) {
                auto* r = static_cast<RIL_SimRefreshResponse_v7*>(data);
                sv.sim->ind()->simRefresh(kUnsol, {.type = r->result,
                                                   .efId = r->ef_id,
                                                   .aid = str(r->aid)});
            }
            break;
        case RIL_UNSOL_CALL_RING:
            if (sv.voice && sv.voice->ind()) sv.voice->ind()->callRing(kUnsol, true, {});
            break;
        case RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED:
            if (sv.sim && sv.sim->ind()) sv.sim->ind()->simStatusChanged(kUnsol);
            break;
        case RIL_UNSOL_RESTRICTED_STATE_CHANGED:
            if (sv.network && sv.network->ind())
                sv.network->ind()->restrictedStateChanged(
                        kUnsol, static_cast<anet::PhoneRestrictedState>(intAt(data, len, 0)));
            break;
        case RIL_UNSOL_ENTER_EMERGENCY_CALLBACK_MODE:
            if (sv.voice && sv.voice->ind()) sv.voice->ind()->enterEmergencyCallbackMode(kUnsol);
            break;
        case RIL_UNSOL_EXIT_EMERGENCY_CALLBACK_MODE:
            if (sv.voice && sv.voice->ind()) sv.voice->ind()->exitEmergencyCallbackMode(kUnsol);
            break;
        case RIL_UNSOL_RINGBACK_TONE:
            if (sv.voice && sv.voice->ind())
                sv.voice->ind()->indicateRingbackTone(kUnsol, intAt(data, len, 0) != 0);
            break;
        case RIL_UNSOL_RESEND_INCALL_MUTE:
            if (sv.voice && sv.voice->ind()) sv.voice->ind()->resendIncallMute(kUnsol);
            break;
        case RIL_UNSOL_VOICE_RADIO_TECH_CHANGED:
            if (sv.network && sv.network->ind())
                sv.network->ind()->voiceRadioTechChanged(
                        kUnsol, static_cast<aidl_radio::RadioTechnology>(intAt(data, len, 0)));
            break;
        case 11017:  // Samsung: RIL_UNSOL_WB_AMR_STATE, one int
            callAudioWideband(intAt(data, len, 0) != 0);
            break;
        default:
            LOG(DEBUG) << "unsolicited " << id << " (" << len << " bytes) dropped";
            break;
    }
}

}  // namespace android::hardware::radio::n7100
