namespace demo {

cclass Registry
{
public:
    static cfield() int count;
    cfield() static int total;
    cfunc static Registry& Instance();
    rkfunc(static) int Size();
    static rkfunc void Clear();
};

cstruct Defaults
{
    static int shared;
    int local;
};

}
