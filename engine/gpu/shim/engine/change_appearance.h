// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace BbEngine {
/// The hunter's preview is on screen: character creation at the title, or the Dream mirror's
/// editor (change_appearance.cpp holds their list's step while it is open). Read by the keyboard
/// and mouse (input/input.cpp), which turn the hunter by dragging there.
bool ChangeAppearancePreview();
}
