#include "node_editor.h"

int foo()
{
    using namespace ax;

    ax::rect xxx(2,3,4,5);



    point a;
    matrix b;
    matrix4 x;

    x.invert();
    point y[13];
    point* z = y;

    transform(y, x);
    transform(z, 10, x);

    auto c = transformed(a, b);
    auto d = transformed_v(a, b);

    auto c2 = transformed(a, x);
    auto d2 = transformed_v(a, x);



    return 0;
}