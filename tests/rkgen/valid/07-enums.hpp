namespace demo {

cenum class Color : std::uint8_t
{
    Red,
    Green = 2,
    Blue,
    Mask = 0xFFu,
    After,
};

rkenum Plain
{
    First = 10, Second, Third
};

cclass Holder
{
public:
    rkenum class Mode : int
    {
        Off,
        On,
    };
    cfield() Mode mode;
};

}
