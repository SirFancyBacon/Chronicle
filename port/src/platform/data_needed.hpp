#pragma once

namespace dc::platform {

/**
 * Stops the game before it starts when it has no data, or data of an older layout than it reads (Noktus's convert/data_version.hpp):
 * the data is set up by Noktus's launcher, beside the game. With a window, a message says so and offers to open the launcher; headless, or
 * with no display, one line on stderr does. Either way the game exits with kExitNoData. Data newer than the game knows is only noted.
 */
void RequireCurrentData(bool headless);

} // namespace dc::platform
