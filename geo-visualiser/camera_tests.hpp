#pragma once
#include "viewport.hpp"
void cameraMappingChecks();
void cameraRenderChecks(inspector::Viewport&, const inspector::Display&);
void cameraSnapClearanceChecks(inspector::Viewport&, const inspector::Display& candidate);
