namespace demo {

rkattr(Category, Editor)
cclass Widget
{
public:
    cfield(get=GetWidth, set=SetWidth) int width;
    cfield(readonly) int id;
    cfield(transient) void* cache;
    cfield(attr_hidden, attr_group) int flags;
    cfield(GetA, SetA, extra) int a;
    rkattr(Animatable)
    cfield() float opacity;
    rkattr(Command)
    cfunc void Click();
};

}
