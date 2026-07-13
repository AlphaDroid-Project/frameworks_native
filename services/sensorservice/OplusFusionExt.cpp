/*
 * OPLUS fusion light sensor extension loader — see OplusFusionExt.h.
 */
#undef LOG_TAG
#define LOG_TAG "OplusFusionExt"

#include "OplusFusionExt.h"

#include <cutils/properties.h>
#include <dlfcn.h>
#include <log/log.h>

namespace android {

// --- base-class symbols the prebuilt engine links against -------------------
// The engine's ExtendedFactory : SensorServiceExtFactory. Its ctor calls this
// base ctor (which sets a transient base vtable), then installs its own vtable.
// The object is vtable-only (8 bytes); we never virtual-dispatch through this
// declaration (we use raw vtable indices) and never `delete` the factory through
// a SensorServiceExtFactory* (it's a process-lifetime singleton, intentionally
// leaked) — so the fact that our base's vtable layout differs from the engine's
// is irrelevant at runtime.
SensorServiceExtFactory::SensorServiceExtFactory() {}
SensorServiceExtFactory::~SensorServiceExtFactory() {}

namespace {

// vtable indices decoded from libsensorserviceextimpl.so (ACE3_400, Project 23801)
// via llvm-readelf. Functions start at (vtable_symbol + 0x10), one pointer / 8 bytes.
constexpr int kFactory_createSensorServiceExt = 0;  // -> ISensorServiceExt*
constexpr int kFactory_createSensorDeviceExt  = 3;  // -> SensorDeviceExt*
constexpr int kSvcExt_registerOplusCustomizeSensor = 0;  // (sensor_t*, count, SensorService*)
constexpr int kDevExt_activateFusionSensor    = 5;  // (int handle, bool enabled)
constexpr int kDevExt_batchFusionSensor       = 6;  // (int handle, long, long)

void* gServiceExt = nullptr;
void* gDeviceExt = nullptr;

inline void** vtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

// this-call, no args, returns a pointer
inline void* callP(void* obj, int idx) {
    return reinterpret_cast<void* (*)(void*)>(vtableOf(obj)[idx])(obj);
}

} // namespace

void loadOplusFusionSensors(SensorService* service, const sensor_t* list, size_t count) {
    if (!property_get_bool("persist.alpha.fusion_light", false)) {
        return;
    }

    void* h = dlopen("libsensorserviceextimpl.so", RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) {
        ALOGE("fusion: dlopen(libsensorserviceextimpl.so) failed: %s", dlerror());
        return;
    }

    // extern "C" createExtendedFactory() -> ExtendedFactory* (: SensorServiceExtFactory)
    using CreateFactoryFn = void* (*)();
    auto createExtendedFactory =
            reinterpret_cast<CreateFactoryFn>(dlsym(h, "createExtendedFactory"));
    if (createExtendedFactory == nullptr) {
        ALOGE("fusion: dlsym(createExtendedFactory) failed: %s", dlerror());
        return;
    }

    void* factory = createExtendedFactory();
    if (factory == nullptr) {
        ALOGE("fusion: createExtendedFactory returned null");
        return;
    }

    // svcExt = factory->createSensorServiceExt();  (factory vtable slot 0)
    gServiceExt = callP(factory, kFactory_createSensorServiceExt);
    // devExt = factory->createSensorDeviceExt();    (factory vtable slot 3) — for activate/batch
    gDeviceExt = callP(factory, kFactory_createSensorDeviceExt);
    if (gServiceExt == nullptr) {
        ALOGE("fusion: createSensorServiceExt returned null");
        return;
    }
    // gDeviceExt drives the CWB screenshot compensation monitor via
    // activateFusionSensor (see oplusFusionActivate + SensorDevice::activate).
    ALOGI("fusion: createSensorDeviceExt -> %p (%s)", gDeviceExt,
          gDeviceExt ? "compensation hook ARMED" : "NULL, compensation hook DISABLED");

    // Replicate stock SensorService's per-sensor onSensorFound() callback, which
    // ColorOS invokes for every enumerated HAL sensor. It drives
    // OplusSensorServiceUtils::setFusionLightStatue(sensor.type), which sets the
    // fusion capability flag bytes in the OplusSensorServiceUtils singleton — e.g.
    // seeing qti.sensor.high_pwm_rgb (type 33171070) sets the "Next Gen" enable byte
    // [utils+0x3fb]=1. WITHOUT this, those bytes stay zero (setFusionLightStatue is
    // never called), so SensorDeviceExtImpl::activateFusionSensor() reads [0x3fb]==0
    // and bails with -ENODEV — meaning OplusFusionLightNextGen::activateInternal (the
    // CWB screenshot compensation monitor) never starts and the lux stays content-
    // polluted (R/G/B screen subtraction = 0). The engine's registration gates were
    // force-patched, but the runtime activate path reads the real flag bytes, so we
    // must set them the stock way. onSensorFound is not a vtable method and ignores
    // its `this` (it operates on the process-wide singleton) — resolve it by symbol.
    using OnSensorFoundFn = void (*)(void*, const sensor_t*, size_t);
    auto onSensorFound = reinterpret_cast<OnSensorFoundFn>(
            dlsym(h, "_ZN7android20SensorServiceExtImpl13onSensorFoundEPK8sensor_tm"));
    if (onSensorFound != nullptr) {
        for (size_t i = 0; i < count; ++i) {
            onSensorFound(gServiceExt, list, i);
        }
        ALOGI("fusion: onSensorFound driven for %zu sensors (fusion capability flags set)",
              count);
    } else {
        ALOGE("fusion: dlsym(onSensorFound) failed: %s -- fusion flags stay unset, "
              "compensation will NOT start", dlerror());
    }

    // svcExt->registerOplusCustomizeSensor(list, count, service);  (ISensorServiceExt slot 0)
    // Inspects the HAL sensor list, builds the fusion light sensor and registers it
    // via SensorService::registerSensor (our exported symbol).
    using RegisterFn = void (*)(void*, const sensor_t*, unsigned long, SensorService*);
    auto reg = reinterpret_cast<RegisterFn>(
            vtableOf(gServiceExt)[kSvcExt_registerOplusCustomizeSensor]);
    reg(gServiceExt, list, static_cast<unsigned long>(count), service);

    ALOGI("fusion: registerOplusCustomizeSensor done (fusion light sensor should now register)");
}

bool oplusFusionActive() { return gDeviceExt != nullptr; }

void oplusFusionActivate(int handle, bool enabled) {
    if (gDeviceExt == nullptr) return;
    // activateFusionSensor returns -ENODEV(-19) for non-fusion handles (no-op) and
    // drives OplusFusionLightNextGen::activateInternal (start/stop the CWB screenshot
    // compensation monitor) for the fusion light(0x3e9)/RGB(0x3f0) handles. Log only
    // the meaningful calls so we can confirm the compensation trigger fires at runtime.
    int ret = reinterpret_cast<int (*)(void*, int, bool)>(
            vtableOf(gDeviceExt)[kDevExt_activateFusionSensor])(gDeviceExt, handle, enabled);
    if (ret != -19) {
        ALOGI("fusion: activateFusionSensor(handle=0x%x, enabled=%d) -> %d "
              "(drove NextGen activateInternal / CWB screenshot monitor)",
              handle, enabled, ret);
    }
}

void oplusFusionBatch(int handle, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs) {
    if (gDeviceExt == nullptr) return;
    reinterpret_cast<void (*)(void*, int, long, long)>(
            vtableOf(gDeviceExt)[kDevExt_batchFusionSensor])(
            gDeviceExt, handle, static_cast<long>(samplingPeriodNs),
            static_cast<long>(maxBatchReportLatencyNs));
}

} // namespace android
