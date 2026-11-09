// Array extents and bit-field widths are not reflected: implicit fields that use
// them are skipped with a warning.
cstruct Packed
{
    int values[4];
    unsigned flags : 3;
    int plain;
};
