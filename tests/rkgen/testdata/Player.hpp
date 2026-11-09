// Newly authored regression input. See README.md for provenance.
#pragma once

#include "BasicTypes.hpp"
#include <velora/reflect/Markers.hpp>

namespace rkgen_test
{

struct Entity
{
};

rkclass Player : public Entity
{
public:
    rkfield() int hp = 0;
    rkfield(get=GetMana) int mana = 0;
    rkfield(get=GetShield, set=SetShield) int shield = 0;
    rkfield(set=SetName) const char* name = nullptr;
    rkfield(readonly) Vec3 position;
    rkfield(transient) void* runtimeOnly = nullptr;

    explicit Player(int id);

    rkfunc void SetHp(int value);
    rkfunc int GetHp() const noexcept;
    rkfunc static Player* SpawnDefault();

    int GetMana() const;
    int GetShield() const;
    void SetShield(int value);
    void SetName(const char* value);
};

} // namespace rkgen_test
