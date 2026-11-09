#pragma once

namespace demo {

cstruct Vec2
{
    float x;
    float y = 0.0f;
    int ignoredFunction(int value);
};

cclass Counter
{
public:
    cfield() int count;
    cfield(GetLimit) int limit;
    cfield(GetName, SetName) const char* name;

    Counter(int start);

    cfunc void Reset();
    cfunc int Value() const noexcept;
    cfunc static Counter* Make();

private:
    int GetLimit() const;
    int hidden;
};

}
