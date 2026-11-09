namespace demo {

cclass Player
{
public:
    rkctor explicit Player(int id);
    rkctor Player(const Name& name, int level = 1);
    explicit Player(float scale);
    Player();
    cfield() int id;
};

cstruct Point
{
    Point(int x, int y);
    rkctor Point();
    int x;
    int y;
};

}
