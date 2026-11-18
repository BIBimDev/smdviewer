#pragma once
#include "viewport.hpp"
std::size_t visibleContextOutlines(inspector::Viewport&);
void displayMappingChecks(const inspector::Document&);
void displayRenderChecks(inspector::Viewport&, const inspector::Document&);
