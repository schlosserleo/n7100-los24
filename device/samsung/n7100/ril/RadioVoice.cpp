#include "RadioServices.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <thread>

namespace android::hardware::radio::n7100 {

void RadioVoice::voidRequest(int32_t serial, int req, std::unique_ptr<Payload> p,
                             VoidResponse response) {
    ril(req, std::move(p), [this, serial, response](RIL_Errno e, void*, size_t) {
        if (mResp) ((*mResp).*response)(rsp(serial, e));
    });
}

static std::vector<avoice::Call> toCalls(void* r, size_t len) {
    std::vector<avoice::Call> calls;
    if (!r) return calls;
    auto** c = static_cast<RIL_Call**>(r);
    for (size_t i = 0; i < len / sizeof(RIL_Call*); i++) {
        if (!c[i]) continue;
        avoice::Call call{};
        call.state = c[i]->state;
        call.index = c[i]->index;
        call.toa = c[i]->toa;
        call.isMpty = c[i]->isMpty;
        call.isMT = c[i]->isMT;
        call.als = c[i]->als;
        call.isVoice = c[i]->isVoice;
        call.isVoicePrivacy = c[i]->isVoicePrivacy;
        call.number = str(c[i]->number);
        call.numberPresentation = c[i]->numberPresentation;
        call.name = str(c[i]->name);
        call.namePresentation = c[i]->namePresentation;
        calls.push_back(call);
    }
    return calls;
}

/*
 * The modem's side of call audio. On LineageOS 21 the audio HAL sent these through
 * libsecril-client as RIL_REQUEST_OEM_HOOK_RAW (function 0x08 = sound, sub-function, 16 bit
 * length, parameters); the payloads below are the ones recorded there for a handset call.
 * Without them the modem keeps its PCM interface silent. The codec side (WM1811 AIF2) is
 * set up outside this service.
 */
static void oemSound(std::initializer_list<uint8_t> bytes) {
    struct Raw {
        uint8_t b[8];
    };
    auto p = std::make_unique<Payload>();
    auto* raw = p->make<Raw>();
    std::copy(bytes.begin(), bytes.end(), raw->b);
    p->len = bytes.size();
    ril(RIL_REQUEST_OEM_HOOK_RAW, std::move(p), [](RIL_Errno e, void*, size_t) {
        if (e != RIL_E_SUCCESS) LOG(WARNING) << "call audio: OEM sound request failed: " << e;
    });
}

/*
 * What the codec has to do is left in a file for the n7100-voice service, which sets up the
 * WM1811 (it can reach the sound card, this service cannot): "off", or the sample rate of the
 * modem's PCM, 16000 while the call uses AMR-WB and 8000 otherwise.
 */
static std::atomic<bool> gCallAudio{false};
static std::atomic<bool> gWideband{false};

void callAudioPublish() {
    const char* state = !gCallAudio ? "off\n" : gWideband ? "16000\n" : "8000\n";
    int fd = open("/data/vendor/ril/n7100_call_audio", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        LOG(WARNING) << "call audio: state file: " << strerror(errno);
        return;
    }
    if (write(fd, state, strlen(state)) < 0) LOG(WARNING) << "call audio: state file write failed";
    close(fd);
}

void callAudioWideband(bool wideband) {
    if (gWideband.exchange(wideband) != wideband) {
        LOG(INFO) << "call audio: " << (wideband ? "AMR-WB (16 kHz)" : "narrowband (8 kHz)");
        if (gCallAudio) callAudioPublish();
    }
}

/*
 * The modem tunes its echo canceller and gains for the audio path it is told (handset or
 * speaker). The codec service knows which one the call screen selected and leaves it in
 * /data/vendor/ril/n7100_call_route; follow that file for as long as the call lasts.
 */
static std::atomic<int> gRouteWatch{0};

static void watchCallRoute(int generation) {
    std::string last = "earpiece";  // what the start sequence selected
    while (gCallAudio && gRouteWatch == generation) {
        char buf[16] = {};
        if (FILE* f = fopen("/data/vendor/ril/n7100_call_route", "re")) {
            if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
            fclose(f);
        }
        std::string route = buf;
        while (!route.empty() && (route.back() == '\n' || route.back() == ' ')) route.pop_back();
        if ((route == "speaker" || route == "earpiece") && route != last) {
            oemSound({0x08, 0x05, 0x00, 0x06, static_cast<uint8_t>(route == "speaker" ? 0x06 : 0x01), 0xd1});
            LOG(INFO) << "call audio: modem path " << route;
            last = route;
        }
        usleep(300 * 1000);
    }
}

static void updateCallAudio(bool inCall) {
    if (gCallAudio.exchange(inCall) == inCall) return;
    if (inCall) {
        oemSound({0x08, 0x0d, 0x00, 0x06, 0x00, 0x01});  // two-microphone processing on
        oemSound({0x08, 0x0b, 0x00, 0x05, 0x00});        // microphone not muted
        oemSound({0x08, 0x05, 0x00, 0x06, 0x01, 0xd1});  // audio path: handset
        oemSound({0x08, 0x0a, 0x00, 0x05, 0x01});        // PCM clock on
        oemSound({0x08, 0x03, 0x00, 0x06, 0x01, 0x03});  // voice volume 3 of 5
    } else {
        oemSound({0x08, 0x0a, 0x00, 0x05, 0x00});        // PCM clock off
        oemSound({0x08, 0x0d, 0x00, 0x06, 0x00, 0x00});
    }
    callAudioPublish();
    int generation = ++gRouteWatch;
    if (inCall) std::thread(watchCallRoute, generation).detach();
    LOG(INFO) << "call audio " << (inCall ? "on" : "off");
}

ScopedAStatus RadioVoice::getCurrentCalls(int32_t serial) {
    ril(RIL_REQUEST_GET_CURRENT_CALLS, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            auto calls = toCalls(r, len);
            if (e == RIL_E_SUCCESS) {
                // active, holding, dialing or alerting: the call has (or is about to have) audio
                bool audio = false;
                for (const auto& c : calls) audio |= c.state >= 0 && c.state <= 3;
                updateCallAudio(audio);
            }
            if (mResp) mResp->getCurrentCallsResponse(rsp(serial, e), calls);
        });
    return ScopedAStatus::ok();
}

// libsec-ril crashes on a NULL uusInfo (from LineageOS' libsecril-shim)
void RadioVoice::dialRequest(int32_t serial, const avoice::Dial& dialInfo, bool emergency) {
    auto p = std::make_unique<Payload>();
    struct DialData {
        RIL_Dial dial;
        RIL_UUS_Info uus;
    };
    auto* d = p->make<DialData>();
    d->dial.address = p->keep(dialInfo.address);
    d->dial.clir = dialInfo.clir;
    d->dial.uusInfo = &d->uus;
    p->len = sizeof(RIL_Dial);
    ril(RIL_REQUEST_DIAL, std::move(p), [this, serial, emergency](RIL_Errno e, void*, size_t) {
        if (!mResp) return;
        if (emergency)
            mResp->emergencyDialResponse(rsp(serial, e));
        else
            mResp->dialResponse(rsp(serial, e));
    });
}

ScopedAStatus RadioVoice::dial(int32_t serial, const avoice::Dial& dialInfo) {
    dialRequest(serial, dialInfo, false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::emergencyDial(int32_t serial, const avoice::Dial& dialInfo, int32_t,
                                        const std::vector<std::string>&,
                                        avoice::EmergencyCallRouting, bool, bool) {
    dialRequest(serial, dialInfo, true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangup(int32_t serial, int32_t gsmIndex) {
    voidRequest(serial, RIL_REQUEST_HANGUP, Payload::ints({gsmIndex}),
                &avoice::IRadioVoiceResponse::hangupConnectionResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupWaitingOrBackground(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND, Payload::none(),
                &avoice::IRadioVoiceResponse::hangupWaitingOrBackgroundResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupForegroundResumeBackground(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND, Payload::none(),
                &avoice::IRadioVoiceResponse::hangupForegroundResumeBackgroundResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::switchWaitingOrHoldingAndActive(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE, Payload::none(),
                &avoice::IRadioVoiceResponse::switchWaitingOrHoldingAndActiveResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::conference(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_CONFERENCE, Payload::none(),
                &avoice::IRadioVoiceResponse::conferenceResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::acceptCall(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_ANSWER, Payload::none(),
                &avoice::IRadioVoiceResponse::acceptCallResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::rejectCall(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_UDUB, Payload::none(),
                &avoice::IRadioVoiceResponse::rejectCallResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::explicitCallTransfer(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_EXPLICIT_CALL_TRANSFER, Payload::none(),
                &avoice::IRadioVoiceResponse::explicitCallTransferResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::separateConnection(int32_t serial, int32_t gsmIndex) {
    voidRequest(serial, RIL_REQUEST_SEPARATE_CONNECTION, Payload::ints({gsmIndex}),
                &avoice::IRadioVoiceResponse::separateConnectionResponse);
    return ScopedAStatus::ok();
}

// libsec-ril appends a vendor cause; the framework wants just the cause (shim)
ScopedAStatus RadioVoice::getLastCallFailCause(int32_t serial) {
    ril(RIL_REQUEST_LAST_CALL_FAIL_CAUSE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            avoice::LastCallFailCauseInfo info{
                    .causeCode = static_cast<avoice::LastCallFailCause>(intAt(r, len, 0, 0xffff)),
                    .vendorCause = ""};
            if (mResp) mResp->getLastCallFailCauseResponse(rsp(serial, e), info);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendDtmf(int32_t serial, const std::string& s) {
    voidRequest(serial, RIL_REQUEST_DTMF, Payload::string(s),
                &avoice::IRadioVoiceResponse::sendDtmfResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::startDtmf(int32_t serial, const std::string& s) {
    voidRequest(serial, RIL_REQUEST_DTMF_START, Payload::string(s),
                &avoice::IRadioVoiceResponse::startDtmfResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::stopDtmf(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_DTMF_STOP, Payload::none(),
                &avoice::IRadioVoiceResponse::stopDtmfResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setMute(int32_t serial, bool enable) {
    voidRequest(serial, RIL_REQUEST_SET_MUTE, Payload::ints({enable ? 1 : 0}),
                &avoice::IRadioVoiceResponse::setMuteResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getMute(int32_t serial) {
    ril(RIL_REQUEST_GET_MUTE, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->getMuteResponse(rsp(serial, e), intAt(r, len, 0) != 0);
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getClir(int32_t serial) {
    ril(RIL_REQUEST_GET_CLIR, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->getClirResponse(rsp(serial, e), intAt(r, len, 0), intAt(r, len, 1));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setClir(int32_t serial, int32_t status) {
    voidRequest(serial, RIL_REQUEST_SET_CLIR, Payload::ints({status}),
                &avoice::IRadioVoiceResponse::setClirResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getClip(int32_t serial) {
    ril(RIL_REQUEST_QUERY_CLIP, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp)
            mResp->getClipResponse(rsp(serial, e),
                                   static_cast<avoice::ClipStatus>(intAt(r, len, 0, 2)));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getCallWaiting(int32_t serial, int32_t serviceClass) {
    ril(RIL_REQUEST_QUERY_CALL_WAITING, Payload::ints({serviceClass}),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp)
                mResp->getCallWaitingResponse(rsp(serial, e), intAt(r, len, 0) != 0,
                                              intAt(r, len, 1));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setCallWaiting(int32_t serial, bool enable, int32_t serviceClass) {
    voidRequest(serial, RIL_REQUEST_SET_CALL_WAITING, Payload::ints({enable ? 1 : 0, serviceClass}),
                &avoice::IRadioVoiceResponse::setCallWaitingResponse);
    return ScopedAStatus::ok();
}

static std::unique_ptr<Payload> callForwardPayload(const avoice::CallForwardInfo& info) {
    auto p = std::make_unique<Payload>();
    auto* cf = p->make<RIL_CallForwardInfo>();
    cf->status = info.status;
    cf->reason = info.reason;
    cf->serviceClass = info.serviceClass;
    cf->toa = info.toa;
    cf->number = p->keepOrNull(info.number);
    cf->timeSeconds = info.timeSeconds;
    return p;
}

ScopedAStatus RadioVoice::getCallForwardStatus(int32_t serial, const avoice::CallForwardInfo& info) {
    ril(RIL_REQUEST_QUERY_CALL_FORWARD_STATUS, callForwardPayload(info),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            std::vector<avoice::CallForwardInfo> list;
            if (r) {
                auto** cf = static_cast<RIL_CallForwardInfo**>(r);
                for (size_t i = 0; i < len / sizeof(RIL_CallForwardInfo*); i++) {
                    if (!cf[i]) continue;
                    list.push_back({.status = cf[i]->status,
                                    .reason = cf[i]->reason,
                                    .serviceClass = cf[i]->serviceClass,
                                    .toa = cf[i]->toa,
                                    .number = str(cf[i]->number),
                                    .timeSeconds = cf[i]->timeSeconds});
                }
            }
            if (mResp) mResp->getCallForwardStatusResponse(rsp(serial, e), list);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setCallForward(int32_t serial, const avoice::CallForwardInfo& info) {
    voidRequest(serial, RIL_REQUEST_SET_CALL_FORWARD, callForwardPayload(info),
                &avoice::IRadioVoiceResponse::setCallForwardResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendUssd(int32_t serial, const std::string& ussd) {
    voidRequest(serial, RIL_REQUEST_SEND_USSD, Payload::string(ussd),
                &avoice::IRadioVoiceResponse::sendUssdResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::cancelPendingUssd(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_CANCEL_USSD, Payload::none(),
                &avoice::IRadioVoiceResponse::cancelPendingUssdResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::exitEmergencyCallbackMode(int32_t serial) {
    voidRequest(serial, RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE, Payload::none(),
                &avoice::IRadioVoiceResponse::exitEmergencyCallbackModeResponse);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::handleStkCallSetupRequestFromSim(int32_t serial, bool accept) {
    voidRequest(serial, RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM,
                Payload::ints({accept ? 1 : 0}),
                &avoice::IRadioVoiceResponse::handleStkCallSetupRequestFromSimResponse);
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
