#pragma once

// Layout geometry in DIPs, shared by the controls core (measure/arrange overrides) and the legacy
// Framework. Split out of framework.hpp so controls do not depend on Framework's header.

namespace velora
{

struct Size
{
    float width = 0, height = 0;
};

struct Box
{
    float x = 0, y = 0, width = 0, height = 0;
    bool contains(float px, float py) const
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

} // namespace velora
