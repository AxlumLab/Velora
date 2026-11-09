// Negative literals are evaluated; after an unknown initializer the implicit
// values that follow are unknown too.
cenum class Offset : int
{
    Below = -5,
    Next,
    Shifted = 1 << 2,
    AfterShift,
};
