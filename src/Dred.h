#pragma once
struct ID3D12Device;
namespace rf::dred {
void beforeDevice();             // before the game makes its device: turn DRED on (only with config/Dred.txt)
void watch(ID3D12Device* dev);   // the game's device: report if it is removed / the driver faults
void check();                    // once per Present
bool on();
} // namespace rf::dred
