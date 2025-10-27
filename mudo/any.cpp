

#include <iostream>
#include <typeinfo>
#include <cassert>
// 通用容器 保存各种数据
class Any
{

private:
    class holder // 嵌套的父类  只需要几个虚函数就可以了
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info &type() = 0; // 获取保存的数据类型
        virtual holder *clone() = 0;              // 能够针对一个子类对象克隆出一个新的对象出来
    };

    template <class T>                // 模板类
    class placeholder : public holder // 子类 继承了父类
    {
    public:
        placeholder(const T &val) : _val(val) {}

        virtual const std::type_info &type() // 获取子类对象保存的数据类型
        {
            return typeid(T);
        }
        virtual holder *clone() // 针对当前子类对象克隆一个新的子类对象出来
        {
            return new placeholder(_val);
        }

    public: // 有一个成员 保存任意类型的数据  可以返回类型
        T _val;
    };

    holder *_content; // 父类指针

public:
    Any() : _content(nullptr) {} // 空构造
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) // 直接给数据构造通用容器
    {
        // 考虑为val new一个子类对象出来
    }
    Any(const Any &other) : _content(other._content ? other._content->clone() : nullptr) // 也可能给一个容器构造一个新的容器
    {
        // 首先判断通融容器内部有没有保存数据 有就克隆一份保存 但是不能保存你保存的指针
    }
    ~Any()
    {
        delete _content;
    }
    Any &swap(Any &other)
    {
        std::swap(_content, other._content); // 交换一下指针
        return *this;
    }

    template <class T>
    T *get() // 返回子类对象保存的数据的指针
    {
        assert(typeid(T) == _content->type());      // 想要获取的数据类型必须和保存的数据类型一致
        return &((placeholder<T> *)_content)->_val; // 先类型转换 再取地址
    }

    // 重载等号运算符
    template <class T>
    Any &operator=(const T &val)
    {
        // 为val构造一个临时的通用容器 然后与当前容器自身(this)进行指针交换 临时对象释放的时候 原先保存的数据也就被释放掉了
        Any(val).swap(*this);
        return *this;
    }

    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

class Test
{
public:
    Test() { std::cout << "构造" << std::endl; }
    Test(const Test &t) { std::cout << "拷贝构造" << std::endl; }
    ~Test() { std::cout << "析构" << std::endl; }
};
int main()
{
    Any a;
    {
        Test t;
        a = t;
    }

    a = 10;

    int *pa = a.get<int>();
    std::cout << *pa << std::endl;

    a = std::string("你好");
    std::string *ps = a.get<std::string>();
    std::cout << *ps << std::endl;
    return 0;
}