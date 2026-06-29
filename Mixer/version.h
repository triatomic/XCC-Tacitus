#pragma once

// Single source of truth for the Mixer's display version. Bump this once per
// release; the Configure-menu version line (and any future title/About text)
// reads from here. Keep in sync with the git tag / GitHub release (vX.YZ).
#define XCC_MIXER_VERSION "12.90"
#define XCC_MIXER_EDITION "XCC Mixer: Tacitus Edition"
#define XCC_MIXER_VERSION_LABEL XCC_MIXER_EDITION " v" XCC_MIXER_VERSION
