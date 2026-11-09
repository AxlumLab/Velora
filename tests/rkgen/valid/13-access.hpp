namespace demo {

cclass Account
{
    cfield() int implicitPrivate;
public:
    cfield() int balance;
    cfunc int Balance() const;
protected:
    cfield() int audit;
    cfunc void Audit();
private:
    cfunc void Lock();
    rkctor Account(int id);
public:
    cfield() int owner;
};

cstruct Record : Base
{
    int visible;
private:
    int secret;
protected:
    cfield() float weight;
};

}
