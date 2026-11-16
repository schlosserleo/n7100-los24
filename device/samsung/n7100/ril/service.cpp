/*
 * AIDL radio HAL for the Galaxy Note II (GT-N7100): Samsung's legacy RIL
 * (libsec-ril.so) on the XMM6262, behind the radio AIDL interfaces of Android 17
 * (its telephony stack dropped HIDL radio < 1.4, and libril only has 1.1).
 */
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "RadioServices.h"

using namespace android::hardware::radio::n7100;

template <class T>
static std::shared_ptr<T> publish(const char* instance) {
    auto svc = ndk::SharedRefBase::make<T>();
    std::string name = std::string(T::descriptor) + "/" + instance;
    auto status = AServiceManager_addService(svc->asBinder().get(), name.c_str());
    CHECK_EQ(status, STATUS_OK) << name;
    LOG(INFO) << "published " << name;
    return svc;
}

int main() {
    android::base::InitLogging(nullptr, android::base::LogdLogger(android::base::RADIO));
    android::base::SetDefaultTag("radio-n7100");
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    // Not from vendor.rild.libpath: that would wake AOSP's rild (telephony_vendor.mk) too.
    // The "-d" device is what LineageOS 21 passed (vendor.rild.libargs); libsec-ril
    // opens /dev/umts_* itself.
    const char* lib = "/vendor/lib/libsec-ril.so";
    std::vector<std::string> args = {"rild", "-d", "/dev/ttyS0"};
    if (!n7100ril::start(lib, args, onUnsolicited)) {
        LOG(ERROR) << "vendor RIL failed to start";
    }

    callAudioPublish();
    auto& sv = services();
    sv.config = publish<RadioConfig>("default");
    sv.modem = publish<RadioModem>("slot1");
    sv.sim = publish<RadioSim>("slot1");
    sv.network = publish<RadioNetwork>("slot1");
    sv.data = publish<RadioData>("slot1");
    sv.voice = publish<RadioVoice>("slot1");
    sv.messaging = publish<RadioMessaging>("slot1");

    ABinderProcess_joinThreadPool();
    return 1;
}
