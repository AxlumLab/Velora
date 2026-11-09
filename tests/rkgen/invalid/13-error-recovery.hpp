// expect-error: 4 9
cclass First
{
    cfunc int Broken;
    cfield() int ok;
};
cclass Second
{
    cfunc int Other;
};
