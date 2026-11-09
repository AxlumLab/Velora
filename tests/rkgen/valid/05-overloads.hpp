namespace demo {

cclass Shape : public Base
{
public:
    cfunc virtual int Area() const = 0;
    cfunc auto Size() const -> int;
    cfunc void Resize(int width, int height = 10);
    cfunc void Resize(float scale);
    cfunc void Swap(Shape& other) noexcept;
    cfunc void Take(std::vector<int>&& values);
    cfunc const char* Name() const;
    cfunc bool Contains(const Point& point, float tolerance = 0.5f) const noexcept;
    cfunc void Visit(void);
};

}
