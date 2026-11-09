// rkgen: -DVELORA_API
#define LOCAL_EXPORT

namespace demo {

rkclass VELORA_API Player final : public Entity
{
    R_OBJECT_NO_SPAWN(Player)
public:
    cfield() int hp;
    cfunc VELORA_API int GetHp() const;
    cfunc LOCAL_EXPORT void Heal(int amount);
    rkfunc static Player* Spawn();
};

cclass Npc
{
    R_OBJECT(Npc)
public:
    rkfield(GetMood) int mood;
    cfunc int GetMood() const;
};

}
