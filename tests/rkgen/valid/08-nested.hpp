namespace outer {
namespace inner::deep {

cclass Node
{
public:
    rkstruct Link
    {
        int target;
    };
    cfield() Link first;
};

}

cstruct Root
{
    int value;
};

}

namespace {
cstruct Hidden
{
    int x;
};
}

namespace other {
cstruct Second
{
    double weight;
};
}
