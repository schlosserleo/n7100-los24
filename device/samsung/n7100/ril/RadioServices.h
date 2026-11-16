#pragma once

#include "gen/RadioConfigBase.h"
#include "gen/RadioDataBase.h"
#include "gen/RadioMessagingBase.h"
#include "gen/RadioModemBase.h"
#include "gen/RadioNetworkBase.h"
#include "gen/RadioSimBase.h"
#include "gen/RadioVoiceBase.h"

#include <atomic>
#include <mutex>

namespace android::hardware::radio::n7100 {

namespace aconfig = ::aidl::android::hardware::radio::config;
namespace adata = ::aidl::android::hardware::radio::data;
namespace amsg = ::aidl::android::hardware::radio::messaging;
namespace amodem = ::aidl::android::hardware::radio::modem;
namespace anet = ::aidl::android::hardware::radio::network;
namespace asim = ::aidl::android::hardware::radio::sim;
namespace avoice = ::aidl::android::hardware::radio::voice;
using ndk::ScopedAStatus;

// What the indications and some responses need to know across services
struct RilState {
    std::mutex lock;
    std::string operatorNumeric;  // last OPERATOR response
    int dataRat = 0;              // RIL_RadioTechnology of the last data registration
    int cardState = 0;            // RIL_CardState of the last GET_SIM_STATUS
    std::string iccid;            // EF_ICCID of the present card (digits), read once per card
    int legacyRadioState = RADIO_STATE_UNAVAILABLE;
};
RilState& state();

class RadioConfig : public RadioConfigBase {
  public:
    ScopedAStatus getHalDeviceCapabilities(int32_t serial) override;
    ScopedAStatus getNumOfLiveModems(int32_t serial) override;
    ScopedAStatus getPhoneCapability(int32_t serial) override;
    ScopedAStatus getSimSlotsStatus(int32_t serial) override;
    ScopedAStatus setNumOfLiveModems(int32_t serial, int8_t n) override;
    ScopedAStatus setPreferredDataModem(int32_t serial, int8_t modemId) override;
    ScopedAStatus setSimSlotsMapping(int32_t serial,
                                     const std::vector<aconfig::SlotPortMapping>& map) override;
};

class RadioModem : public RadioModemBase {
  public:
    ScopedAStatus enableModem(int32_t serial, bool on) override;
    ScopedAStatus getBasebandVersion(int32_t serial) override;
    ScopedAStatus getDeviceIdentity(int32_t serial) override;
    ScopedAStatus getImei(int32_t serial) override;
    ScopedAStatus getModemActivityInfo(int32_t serial) override;
    ScopedAStatus getModemStackStatus(int32_t serial) override;
    ScopedAStatus getRadioCapability(int32_t serial) override;
    ScopedAStatus requestShutdown(int32_t serial) override;
    ScopedAStatus sendDeviceState(int32_t serial, amodem::DeviceStateType type, bool state) override;
    ScopedAStatus setRadioPower(int32_t serial, bool on, bool forEmergency,
                                bool preferredForEmergency) override;
    void onResponseFunctionsSet() override;

  private:
    void getImeiAndSv(std::function<void(RIL_Errno, std::string, std::string)> done);
};

class RadioSim : public RadioSimBase {
  public:
    ScopedAStatus areUiccApplicationsEnabled(int32_t serial) override;
    ScopedAStatus changeIccPin2ForApp(int32_t serial, const std::string& oldPin2,
                                      const std::string& newPin2, const std::string& aid) override;
    ScopedAStatus changeIccPinForApp(int32_t serial, const std::string& oldPin,
                                     const std::string& newPin, const std::string& aid) override;
    ScopedAStatus getFacilityLockForApp(int32_t serial, const std::string& facility,
                                        const std::string& password, int32_t serviceClass,
                                        const std::string& appId) override;
    ScopedAStatus getIccCardStatus(int32_t serial) override;
    ScopedAStatus getImsiForApp(int32_t serial, const std::string& aid) override;
    ScopedAStatus iccIoForApp(int32_t serial, const asim::IccIo& iccIo) override;
    ScopedAStatus reportStkServiceIsRunning(int32_t serial) override;
    ScopedAStatus sendEnvelope(int32_t serial, const std::string& contents) override;
    ScopedAStatus sendTerminalResponseToSim(int32_t serial, const std::string& contents) override;
    ScopedAStatus setFacilityLockForApp(int32_t serial, const std::string& facility, bool lockState,
                                        const std::string& password, int32_t serviceClass,
                                        const std::string& appId) override;
    ScopedAStatus setSimCardPower(int32_t serial, asim::CardPowerState powerUp) override;
    ScopedAStatus supplyIccPin2ForApp(int32_t serial, const std::string& pin2,
                                      const std::string& aid) override;
    ScopedAStatus supplyIccPinForApp(int32_t serial, const std::string& pin,
                                     const std::string& aid) override;
    ScopedAStatus supplyIccPuk2ForApp(int32_t serial, const std::string& puk2,
                                      const std::string& pin2, const std::string& aid) override;
    ScopedAStatus supplyIccPukForApp(int32_t serial, const std::string& puk, const std::string& pin,
                                     const std::string& aid) override;

  private:
    using PinResponse = ndk::ScopedAStatus (asim::IRadioSimResponse::*)(
            const aidl_radio::RadioResponseInfo&, int32_t);
    void pinRequest(int32_t serial, int req, std::vector<std::optional<std::string>> args,
                    PinResponse response);
};

class RadioNetwork : public RadioNetworkBase {
  public:
    ScopedAStatus getAllowedNetworkTypesBitmap(int32_t serial) override;
    ScopedAStatus getAvailableNetworks(int32_t serial) override;
    ScopedAStatus getCellInfoList(int32_t serial) override;
    ScopedAStatus getDataRegistrationState(int32_t serial) override;
    ScopedAStatus getNetworkSelectionMode(int32_t serial) override;
    ScopedAStatus getOperator(int32_t serial) override;
    ScopedAStatus getSignalStrength(int32_t serial) override;
    ScopedAStatus getVoiceRadioTechnology(int32_t serial) override;
    ScopedAStatus getVoiceRegistrationState(int32_t serial) override;
    ScopedAStatus setAllowedNetworkTypesBitmap(int32_t serial, int32_t bitmap) override;
    ScopedAStatus setBarringPassword(int32_t serial, const std::string& facility,
                                     const std::string& oldPassword,
                                     const std::string& newPassword) override;
    ScopedAStatus setCellInfoListRate(int32_t serial, int32_t rate) override;
    ScopedAStatus setIndicationFilter(int32_t serial, int32_t filter) override;
    ScopedAStatus setLinkCapacityReportingCriteria(int32_t serial, int32_t hysteresisMs,
                                                   int32_t hysteresisDlKbps,
                                                   int32_t hysteresisUlKbps,
                                                   const std::vector<int32_t>& dl,
                                                   const std::vector<int32_t>& ul,
                                                   aidl_radio::AccessNetwork an) override;
    ScopedAStatus setLocationUpdates(int32_t serial, bool enable) override;
    ScopedAStatus setNetworkSelectionModeAutomatic(int32_t serial) override;
    ScopedAStatus setNetworkSelectionModeManual(int32_t serial, const std::string& operatorNumeric,
                                                aidl_radio::AccessNetwork ran) override;
    ScopedAStatus setSignalStrengthReportingCriteria(
            int32_t serial, const std::vector<anet::SignalThresholdInfo>& infos) override;
    ScopedAStatus setSuppServiceNotifications(int32_t serial, bool enable) override;
    ScopedAStatus setSystemSelectionChannels(
            int32_t serial, bool specifyChannels,
            const std::vector<anet::RadioAccessSpecifier>& specifiers) override;

    static anet::SignalStrength toSignalStrength(void* r, size_t len);

  private:
    anet::RegStateResult toRegState(const std::vector<std::string>& s, bool voice);
};

class RadioData : public RadioDataBase {
  public:
    ScopedAStatus deactivateDataCall(int32_t serial, int32_t cid,
                                     adata::DataRequestReason reason) override;
    ScopedAStatus getDataCallList(int32_t serial) override;
    ScopedAStatus setDataAllowed(int32_t serial, bool allow) override;
    ScopedAStatus setDataProfile(int32_t serial,
                                 const std::vector<adata::DataProfileInfo>& profiles) override;
    ScopedAStatus setInitialAttachApn(int32_t serial,
                                      const std::optional<adata::DataProfileInfo>& info) override;
    ScopedAStatus setupDataCall(int32_t serial, aidl_radio::AccessNetwork accessNetwork,
                                const adata::DataProfileInfo& dataProfileInfo,
                                bool roamingAllowed, adata::DataRequestReason reason,
                                const std::vector<adata::LinkAddress>& addresses,
                                const std::vector<std::string>& dnses, int32_t pduSessionId,
                                const std::optional<adata::SliceInfo>& sliceInfo,
                                bool matchAllRuleAllowed) override;

    static std::vector<adata::SetupDataCallResult> toDataCallList(void* r, size_t len);
};

class RadioVoice : public RadioVoiceBase {
  public:
    ScopedAStatus acceptCall(int32_t serial) override;
    ScopedAStatus cancelPendingUssd(int32_t serial) override;
    ScopedAStatus conference(int32_t serial) override;
    ScopedAStatus dial(int32_t serial, const avoice::Dial& dialInfo) override;
    ScopedAStatus emergencyDial(int32_t serial, const avoice::Dial& dialInfo, int32_t categories,
                                const std::vector<std::string>& urns,
                                avoice::EmergencyCallRouting routing,
                                bool hasKnownUserIntentEmergency, bool isTesting) override;
    ScopedAStatus exitEmergencyCallbackMode(int32_t serial) override;
    ScopedAStatus explicitCallTransfer(int32_t serial) override;
    ScopedAStatus getCallForwardStatus(int32_t serial, const avoice::CallForwardInfo& info) override;
    ScopedAStatus getCallWaiting(int32_t serial, int32_t serviceClass) override;
    ScopedAStatus getClip(int32_t serial) override;
    ScopedAStatus getClir(int32_t serial) override;
    ScopedAStatus getCurrentCalls(int32_t serial) override;
    ScopedAStatus getLastCallFailCause(int32_t serial) override;
    ScopedAStatus getMute(int32_t serial) override;
    ScopedAStatus handleStkCallSetupRequestFromSim(int32_t serial, bool accept) override;
    ScopedAStatus hangup(int32_t serial, int32_t gsmIndex) override;
    ScopedAStatus hangupForegroundResumeBackground(int32_t serial) override;
    ScopedAStatus hangupWaitingOrBackground(int32_t serial) override;
    ScopedAStatus rejectCall(int32_t serial) override;
    ScopedAStatus sendDtmf(int32_t serial, const std::string& s) override;
    ScopedAStatus sendUssd(int32_t serial, const std::string& ussd) override;
    ScopedAStatus separateConnection(int32_t serial, int32_t gsmIndex) override;
    ScopedAStatus setCallForward(int32_t serial, const avoice::CallForwardInfo& info) override;
    ScopedAStatus setCallWaiting(int32_t serial, bool enable, int32_t serviceClass) override;
    ScopedAStatus setClir(int32_t serial, int32_t status) override;
    ScopedAStatus setMute(int32_t serial, bool enable) override;
    ScopedAStatus startDtmf(int32_t serial, const std::string& s) override;
    ScopedAStatus stopDtmf(int32_t serial) override;
    ScopedAStatus switchWaitingOrHoldingAndActive(int32_t serial) override;

  private:
    using VoidResponse = ndk::ScopedAStatus (avoice::IRadioVoiceResponse::*)(
            const aidl_radio::RadioResponseInfo&);
    void voidRequest(int32_t serial, int req, std::unique_ptr<Payload> p, VoidResponse response);
    void dialRequest(int32_t serial, const avoice::Dial& dialInfo, bool emergency);
};

class RadioMessaging : public RadioMessagingBase {
  public:
    ScopedAStatus acknowledgeLastIncomingGsmSms(int32_t serial, bool success,
                                                amsg::SmsAcknowledgeFailCause cause) override;
    ScopedAStatus deleteSmsOnSim(int32_t serial, int32_t index) override;
    ScopedAStatus getSmscAddress(int32_t serial) override;
    ScopedAStatus reportSmsMemoryStatus(int32_t serial, bool available) override;
    ScopedAStatus sendSms(int32_t serial, const amsg::GsmSmsMessage& message) override;
    ScopedAStatus sendSmsExpectMore(int32_t serial, const amsg::GsmSmsMessage& message) override;
    ScopedAStatus setGsmBroadcastActivation(int32_t serial, bool activate) override;
    ScopedAStatus setGsmBroadcastConfig(
            int32_t serial, const std::vector<amsg::GsmBroadcastSmsConfigInfo>& configInfo) override;
    ScopedAStatus setSmscAddress(int32_t serial, const std::string& smsc) override;
    ScopedAStatus writeSmsToSim(int32_t serial, const amsg::SmsWriteArgs& args) override;

  private:
    void sendGsmSms(int32_t serial, const amsg::GsmSmsMessage& message, bool expectMore);
};

struct Services {
    std::shared_ptr<RadioConfig> config;
    std::shared_ptr<RadioModem> modem;
    std::shared_ptr<RadioSim> sim;
    std::shared_ptr<RadioNetwork> network;
    std::shared_ptr<RadioData> data;
    std::shared_ptr<RadioVoice> voice;
    std::shared_ptr<RadioMessaging> messaging;
};
Services& services();

// The one SIM slot as the config service reports it, and its "changed" indication
aconfig::SimSlotStatus currentSlotStatus();
void notifySlotStatus();

// Call audio: the modem's PCM state for the codec service (see RadioVoice.cpp)
void callAudioWideband(bool wideband);
void callAudioPublish();

// Unsolicited responses of the legacy RIL -> AIDL indications
void onUnsolicited(int id, const void* data, size_t len);

}  // namespace android::hardware::radio::n7100
