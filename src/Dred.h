#pragma once
struct ID3D12Device;
namespace rf::dred {
void beforeDevice();             // before the game makes its device: turn DRED on (only with config/Dred.txt)
void watch(ID3D12Device* dev);   // the game's device: report if it is removed / the driver faults
void check();                    // once per Present
bool on();
// Always on (no config): a short trail of what RF recorded into which command list, and a crash-safe note the moment a
// command list fails to Close (an invalid call was recorded into it - executing it removes the device) or the device
// is seen removed. Written straight to logs/rf_gpu_crash.txt (survives the crash; latest.log loses its last minute).
void crumb(void const* list, char const* what, unsigned long long a = 0, unsigned long long b = 0, unsigned long long c = 0);
void closeFailed(void const* list, long hr);
} // namespace rf::dred
