#pragma once
// Bedrock Forge client - Auto Sprint, Auto GG, Auto Re-queue (ServerAuto.cpp)
#include <string>
#include <vector>

class IClientInstance;
class LocalPlayer;

namespace bedrock_forge::client::serverauto {
void install();
void uninstall();
/// every HUD frame (client thread): holds sprint, sends queued chat / commands
void frame(IClientInstance& client, LocalPlayer* player, bool playing);
std::vector<std::string> const& ggMessages();
} // namespace bedrock_forge::client::serverauto
