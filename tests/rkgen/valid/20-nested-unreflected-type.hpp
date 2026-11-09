// An unreflected nested type inside a reflected struct is skipped as a whole;
// its closing brace does not end the outer struct.
cstruct Outer
{
    struct Inner
    {
        int a;
    };
    Inner inner;
    int b;
};
