#include "RadioServices.h"

namespace android::hardware::radio::n7100 {

void RadioMessaging::sendGsmSms(int32_t serial, const amsg::GsmSmsMessage& message,
                                bool expectMore) {
    // libsec-ril has no SEND_SMS_EXPECT_MORE (shim): plain SEND_SMS for both
    ril(RIL_REQUEST_SEND_SMS,
        Payload::strings({message.smscPdu.empty() ? std::nullopt : std::optional(message.smscPdu),
                          message.pdu}),
        [this, serial, expectMore](RIL_Errno e, void* r, size_t len) {
            amsg::SendSmsResult res{.messageRef = 0, .ackPDU = "", .errorCode = -1};
            if (r && len >= sizeof(RIL_SMS_Response)) {
                auto* s = static_cast<RIL_SMS_Response*>(r);
                res = {.messageRef = s->messageRef,
                       .ackPDU = str(s->ackPDU),
                       .errorCode = s->errorCode};
            }
            if (!mResp) return;
            if (expectMore)
                mResp->sendSmsExpectMoreResponse(rsp(serial, e), res);
            else
                mResp->sendSmsResponse(rsp(serial, e), res);
        });
}

ScopedAStatus RadioMessaging::sendSms(int32_t serial, const amsg::GsmSmsMessage& message) {
    sendGsmSms(serial, message, false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendSmsExpectMore(int32_t serial, const amsg::GsmSmsMessage& message) {
    sendGsmSms(serial, message, true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::acknowledgeLastIncomingGsmSms(int32_t serial, bool success,
                                                           amsg::SmsAcknowledgeFailCause cause) {
    ril(RIL_REQUEST_SMS_ACKNOWLEDGE, Payload::ints({success ? 1 : 0, static_cast<int>(cause)}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->acknowledgeLastIncomingGsmSmsResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

/*
 * libsec-ril hands out, and takes, the service centre as the hex of its address field
 * ("0791947101673000": length, type of address, BCD digits). Android expects the 3GPP 27.005
 * form <sca>,<tosca> ("\"+491710760300\",145") and would otherwise put the hex string into
 * outgoing messages as if it were a phone number.
 */
static bool isHex(const std::string& s) {
    if (s.empty() || s.size() % 2) return false;
    for (char c : s)
        if (!isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

static bool isAddressField(const std::string& s) {
    return isHex(s) && s.size() >= 4 && strtoul(s.substr(0, 2).c_str(), nullptr, 16) == s.size() / 2 - 1;
}

static std::string smscFromRil(const std::string& s) {
    if (isHex(s) && strtoul(s.substr(0, 2).c_str(), nullptr, 16) == 0) return "";  // none stored
    if (!isAddressField(s)) return s;
    unsigned long toa = strtoul(s.substr(2, 2).c_str(), nullptr, 16);
    std::string num;
    for (size_t i = 4; i + 1 < s.size(); i += 2)
        for (char c : {s[i + 1], s[i]})
            if (c >= '0' && c <= '9') num.push_back(c);
    return "\"" + std::string((toa & 0x70) == 0x10 ? "+" : "") + num + "\"," + std::to_string(toa);
}

static std::string smscToRil(const std::string& s) {
    if (isAddressField(s)) return s;
    std::string num;
    bool plus = false;
    size_t i = 0;
    while (i < s.size() && (s[i] == '"' || s[i] == ' ')) i++;
    if (i < s.size() && s[i] == '+') plus = true, i++;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) num.push_back(s[i]);
    if (num.empty()) return s;
    unsigned long toa = 0;
    size_t comma = s.find(',', i);
    if (comma != std::string::npos) toa = strtoul(s.c_str() + comma + 1, nullptr, 10);
    if (toa == 0 || toa > 255) toa = plus ? 145 : 129;
    char head[8];
    snprintf(head, sizeof(head), "%02zx%02lx", 1 + (num.size() + 1) / 2, toa);
    std::string out = head;
    for (size_t k = 0; k < num.size(); k += 2) {
        out.push_back(k + 1 < num.size() ? num[k + 1] : 'f');
        out.push_back(num[k]);
    }
    return out;
}

ScopedAStatus RadioMessaging::getSmscAddress(int32_t serial) {
    ril(RIL_REQUEST_GET_SMSC_ADDRESS, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t) {
        if (mResp)
            mResp->getSmscAddressResponse(rsp(serial, e), smscFromRil(str(static_cast<char*>(r))));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setSmscAddress(int32_t serial, const std::string& smsc) {
    ril(RIL_REQUEST_SET_SMSC_ADDRESS, Payload::string(smscToRil(smsc)),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setSmscAddressResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::writeSmsToSim(int32_t serial, const amsg::SmsWriteArgs& args) {
    auto p = std::make_unique<Payload>();
    auto* w = p->make<RIL_SMS_WriteArgs>();
    w->status = args.status;
    w->pdu = p->keep(args.pdu);
    w->smsc = p->keepOrNull(args.smsc);
    ril(RIL_REQUEST_WRITE_SMS_TO_SIM, std::move(p), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->writeSmsToSimResponse(rsp(serial, e), intAt(r, len, 0, -1));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::deleteSmsOnSim(int32_t serial, int32_t index) {
    ril(RIL_REQUEST_DELETE_SMS_ON_SIM, Payload::ints({index}), [this, serial](RIL_Errno e, void*, size_t) {
        if (mResp) mResp->deleteSmsOnSimResponse(rsp(serial, e));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::reportSmsMemoryStatus(int32_t serial, bool available) {
    ril(RIL_REQUEST_REPORT_SMS_MEMORY_STATUS, Payload::ints({available ? 1 : 0}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->reportSmsMemoryStatusResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setGsmBroadcastConfig(
        int32_t serial, const std::vector<amsg::GsmBroadcastSmsConfigInfo>& configInfo) {
    auto p = std::make_unique<Payload>();
    struct Configs {
        RIL_GSM_BroadcastSmsConfigInfo* ptrs[32];
        RIL_GSM_BroadcastSmsConfigInfo infos[32];
    };
    auto* c = p->make<Configs>();
    size_t n = std::min<size_t>(configInfo.size(), 32);
    for (size_t i = 0; i < n; i++) {
        c->infos[i] = {.fromServiceId = configInfo[i].fromServiceId,
                       .toServiceId = configInfo[i].toServiceId,
                       .fromCodeScheme = configInfo[i].fromCodeScheme,
                       .toCodeScheme = configInfo[i].toCodeScheme,
                       .selected = configInfo[i].selected};
        c->ptrs[i] = &c->infos[i];
    }
    p->len = n * sizeof(RIL_GSM_BroadcastSmsConfigInfo*);
    ril(RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG, std::move(p),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setGsmBroadcastConfigResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

// legacy: 0 activates, 1 turns off
ScopedAStatus RadioMessaging::setGsmBroadcastActivation(int32_t serial, bool activate) {
    ril(RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION, Payload::ints({activate ? 0 : 1}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setGsmBroadcastActivationResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
