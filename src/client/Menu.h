#pragma once
// Bedrock Forge ClickGUI: Right Shift opens a module menu over a native (empty) dialog screen, which gives us
// the mouse cursor and input focus. Left click toggles a module, right click selects it to edit its settings,
// the wheel scrolls, Esc closes.

class IClientInstance;

namespace bedrock_forge::client::menu {

bool isOpen();
void open(IClientInstance& client);
void install();
void uninstall();

} // namespace bedrock_forge::client::menu
