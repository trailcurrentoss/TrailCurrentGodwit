// Only Music::stateDir(), for tests that save settings beside the HMI's
#include "music.h"

QString Music::stateDir()
{
    return qEnvironmentVariable("STATE_DIRECTORY");
}
