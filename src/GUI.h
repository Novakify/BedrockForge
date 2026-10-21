// Renderer Foundation - overlay windows (modified from BetterRenderDragon's GUI.cpp; see Notice.h).
#pragma once
void initializeImGui();
void updateImGui(); // called on the render thread inside Present, ImGui context locked
