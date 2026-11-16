// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only stand-in for bbhost's guest_abi.h: thd-signaling-test calls bbhost's sceNpSignaling
// functions from the host, so GUEST_ABI is the host's own convention.
#pragma once
#define GUEST_ABI
