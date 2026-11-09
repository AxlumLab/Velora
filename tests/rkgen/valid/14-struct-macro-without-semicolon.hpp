// An R_STRUCT(...) invocation without ';' is a macro line, not a member: the
// following rkfield `count` is still reflected.
cstruct Counter
{
    R_STRUCT(Counter)
    rkfield() int count;
};
