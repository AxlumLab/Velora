/*
 * Multi-line block comment shifts line numbers.
 * namespace fake { cstruct Fake { int f; }; }
 */
namespace demo {

// cstruct Commented { int c; };
cstruct Annotated /* inline comment */
{
    /* field
       comment */
    int value; // trailing
    /** doc */ float scale;
};

}
