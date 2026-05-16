#include "PowerManager.h"
namespace Mortar {
static PowerManager s_instance;
PowerManager* PowerManager::GetInstance() { return &s_instance; }
void PowerManager::Update() {}                  // Defunct: no-op stub
uint32_t PowerManager::GetState() { return 0; } // Defunct: always foreground
}
