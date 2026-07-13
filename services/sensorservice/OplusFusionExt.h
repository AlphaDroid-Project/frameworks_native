/*
 * OPLUS fusion light sensor extension loader.
 *
 * Loads the stock prebuilt system_ext/lib64/libsensorserviceextimpl.so and drives
 * it to register the "OPLUS Fusion Light Sensor Next Gen" (the content-immune ALS
 * stock uses as android.sensor.light) — replacing the raw under-display tcs3720.
 *
 * We do NOT reconstruct the full OPPO SensorServiceExt C++ interface hierarchy.
 * We call the engine's virtuals by their exact vtable index, decoded from the
 * prebuilt via llvm-readelf (see aston-autobrightness.md "FUSION EXT-PORT"):
 *   createExtendedFactory()                 -> extern "C" entry, returns ExtendedFactory*
 *   ExtendedFactory::createSensorServiceExt  = factory vtable index 0
 *   ExtendedFactory::createSensorDeviceExt   = factory vtable index 3
 *   ISensorServiceExt::registerOplusCustomizeSensor(sensor_t*, count, SensorService*)
 *                                            = ISensorServiceExt vtable index 0
 *   SensorDeviceExt::activateFusionSensor(int,bool)   = index 5
 *   SensorDeviceExt::batchFusionSensor(int,long,long) = index 6
 *
 * Gated on persist.alpha.fusion_light (default off) — zero effect on other devices.
 */
#pragma once

#include <hardware/sensors.h>
#include <stddef.h>
#include <stdint.h>

namespace android {

class SensorService;

// Minimal base class the prebuilt engine's ExtendedFactory derives from. Its only
// purpose is to provide the ctor/dtor SYMBOLS the engine links against
// (android::SensorServiceExtFactory::{SensorServiceExtFactory,~}). It is never
// dispatched through nor deleted via this type (see OplusFusionExt.cpp).
class __attribute__((visibility("default"))) SensorServiceExtFactory {
public:
    SensorServiceExtFactory();
    virtual ~SensorServiceExtFactory();
};

// Load + register the OPLUS fusion light sensor. Call once from
// SensorService::onFirstRef() after the AOSP/HAL sensors are enumerated, passing
// the HAL sensor list. No-op unless persist.alpha.fusion_light=1.
void loadOplusFusionSensors(SensorService* service, const sensor_t* list, size_t count);

// Route enable/batch for the fusion sensor to the engine's SensorDeviceExt.
// Safe no-ops if the engine was not loaded.
bool oplusFusionActive();
void oplusFusionActivate(int handle, bool enabled);
void oplusFusionBatch(int handle, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs);

} // namespace android
