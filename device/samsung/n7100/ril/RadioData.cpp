#include "RadioServices.h"

#include <climits>
#include <sstream>

namespace android::hardware::radio::n7100 {

static std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    std::istringstream in(str(s));
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

static adata::PdpProtocolType toProtocol(const std::string& s) {
    if (s == "IP") return adata::PdpProtocolType::IP;
    if (s == "IPV6") return adata::PdpProtocolType::IPV6;
    if (s == "IPV4V6") return adata::PdpProtocolType::IPV4V6;
    if (s == "PPP") return adata::PdpProtocolType::PPP;
    return adata::PdpProtocolType::UNKNOWN;
}

static std::string fromProtocol(adata::PdpProtocolType p) {
    switch (p) {
        case adata::PdpProtocolType::IPV6:
            return "IPV6";
        case adata::PdpProtocolType::IPV4V6:
            return "IPV4V6";
        case adata::PdpProtocolType::PPP:
            return "PPP";
        default:
            return "IP";
    }
}

/*
 * libsec-ril answers with RIL_Data_Call_Response_v6 and puts the gateway into
 * "addresses" (from LineageOS' libsecril-shim: gateways = addresses). The link is
 * point-to-point, so the own address works as the gateway.
 */
std::vector<adata::SetupDataCallResult> RadioData::toDataCallList(void* r, size_t len) {
    std::vector<adata::SetupDataCallResult> list;
    if (!r || len % sizeof(RIL_Data_Call_Response_v6)) return list;
    auto* dc = static_cast<RIL_Data_Call_Response_v6*>(r);
    for (size_t i = 0; i < len / sizeof(RIL_Data_Call_Response_v6); i++) {
        adata::SetupDataCallResult res{};
        res.cause = static_cast<adata::DataCallFailCause>(dc[i].status);
        res.suggestedRetryTime = dc[i].suggestedRetryTime;
        res.cid = dc[i].cid;
        res.active = dc[i].active;
        res.type = toProtocol(str(dc[i].type));
        res.ifname = str(dc[i].ifname);
        for (auto& a : split(dc[i].addresses)) {
            std::string addr = a.find('/') == std::string::npos
                                       ? a + (a.find(':') == std::string::npos ? "/32" : "/64")
                                       : a;
            res.addresses.push_back({.address = addr,
                                     .addressProperties = adata::LinkAddress::ADDRESS_PROPERTY_NONE,
                                     .deprecationTime = INT64_MAX,
                                     .expirationTime = INT64_MAX});
        }
        res.dnses = split(dc[i].dnses);
        res.gateways = split(dc[i].addresses);
        for (auto& g : res.gateways) g = g.substr(0, g.find('/'));
        res.mtuV4 = res.mtuV6 = 0;
        res.handoverFailureMode = adata::SetupDataCallResult::HANDOVER_FAILURE_MODE_LEGACY;
        list.push_back(res);
    }
    return list;
}

ScopedAStatus RadioData::setupDataCall(int32_t serial, aidl_radio::AccessNetwork accessNetwork,
                                       const adata::DataProfileInfo& profile, bool,
                                       adata::DataRequestReason, const std::vector<adata::LinkAddress>&,
                                       const std::vector<std::string>&, int32_t,
                                       const std::optional<adata::SliceInfo>&, bool) {
    // RIL_RadioTechnology + 2 (0/1 mean CDMA/GSM in RIL v4)
    int tech = accessNetwork == aidl_radio::AccessNetwork::GERAN ? RADIO_TECH_GPRS : RADIO_TECH_UMTS;
    ril(RIL_REQUEST_SETUP_DATA_CALL,
        Payload::strings({std::to_string(tech + 2), std::to_string(profile.profileId), profile.apn,
                          profile.user, profile.password,
                          std::to_string(static_cast<int>(profile.authType)),
                          fromProtocol(profile.protocol)}),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            auto list = toDataCallList(r, len);
            adata::SetupDataCallResult res{};
            if (!list.empty()) res = list[0];
            else if (e == RIL_E_SUCCESS) res.cause = adata::DataCallFailCause::ERROR_UNSPECIFIED;
            if (mResp) mResp->setupDataCallResponse(rsp(serial, e), res);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::deactivateDataCall(int32_t serial, int32_t cid,
                                            adata::DataRequestReason reason) {
    // legacy reason: 0 normal, 1 radio shutdown
    ril(RIL_REQUEST_DEACTIVATE_DATA_CALL,
        Payload::strings({std::to_string(cid),
                          reason == adata::DataRequestReason::SHUTDOWN ? "1" : "0"}),
        [this, serial, cid](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->deactivateDataCallResponse(rsp(serial, e));
            if (e != RIL_E_SUCCESS) return;
            // libsec-ril reports no DATA_CALL_LIST_CHANGED for a deactivation it was asked for,
            // and Android's data network stays "disconnecting" (blocking a new setup for five
            // minutes) until the list no longer has the call: report the list ourselves.
            ril(RIL_REQUEST_DATA_CALL_LIST, Payload::none(),
                [this, cid](RIL_Errno, void* r, size_t len) {
                    auto list = toDataCallList(r, len);
                    std::erase_if(list, [cid](const auto& dc) { return dc.cid == cid; });
                    if (mInd) mInd->dataCallListChanged(kUnsol, list);
                });
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::getDataCallList(int32_t serial) {
    ril(RIL_REQUEST_DATA_CALL_LIST, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->getDataCallListResponse(rsp(serial, e), toDataCallList(r, len));
    });
    return ScopedAStatus::ok();
}

// Not in libsec-ril (3G only, no ALLOW_DATA / LTE attach / profiles): accept
ScopedAStatus RadioData::setDataAllowed(int32_t serial, bool) {
    if (mResp) mResp->setDataAllowedResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setDataProfile(int32_t serial, const std::vector<adata::DataProfileInfo>&) {
    if (mResp) mResp->setDataProfileResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setInitialAttachApn(int32_t serial,
                                             const std::optional<adata::DataProfileInfo>&) {
    if (mResp) mResp->setInitialAttachApnResponse(ok(serial));
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
