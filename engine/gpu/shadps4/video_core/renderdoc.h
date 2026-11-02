// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace VideoCore {

// bbport: RenderDoc is not loaded by the port; the command processor's capture points stay as no-ops
// (0.4.4 beta 28: renderdoc.cpp, never built, and the other hooks went).

/// Begins a capture if a renderdoc instance is attached.
inline void StartCapture() {}

/// Ends current renderdoc capture.
inline void EndCapture() {}

} // namespace VideoCore
