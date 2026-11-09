// String literal tokens keep their quotes in attribute text.
rkattr(DisplayName="Player", Order=2)
cclass Player
{
public:
    rkattr(Tooltip="Hit points")
    cfield() int hp;
};
