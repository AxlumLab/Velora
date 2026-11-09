#pragma once
#include <vector>

using u8 = unsigned char;

struct Entity
{
    int id;
};

template<typename T>
rkstruct Box
{
    T value;
};

template<class T> T Max(T a, T b);
cstruct AfterTemplate
{
    int kept;
};

extern int global_counter;

inline int Helper(int x)
{
    return x * 2;
}

namespace demo {
void Free(int a, int b);
cstruct Reflected
{
    using Alias = int;
    typedef float Real;
    friend class Other;
    constexpr static int kLimit = 4;
    int count;
};
}
