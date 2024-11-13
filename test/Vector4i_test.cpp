// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Vector4i.h"

#include "libHh/Array.h"
#include "libHh/RangeOp.h"  // sum()
using namespace hh;

int main() {
  {
    Vector4i v1(1, 2, 3, 4), v2(8, 7, 6, 5);
    SHOW(v1);
    SHOW(v2);
    SHOW(v1[2]);
    SHOW(v2[0]);
    SHOW(v2.with(1, 17));
    SHOW(-v2);
    for (int j : v2) SHOW(j);
    {
      Vector4i v2copy(v2);
      SHOW(v2copy);
    }
    SHOW(v1 + v2);
    SHOW(v1 - v2);
    SHOW(v1 * v2);
    SHOW(v2 * 2);
    SHOW(2 * v2);
    SHOW(v1 - v2 - v1 + v2 * 2);
    SHOW(sum(v2));
    SHOW(v1 * 4);
    SHOW(min(v1 * 4, v2));
    SHOW(max(v1 * 4, v2));
    SHOW(v1 | v2);
    SHOW(v1 & v2);
    SHOW(v1 ^ v2);
    SHOW(v1 << 3);
    SHOW(v2 >> 1);
    SHOW(abs(v2));
    SHOW(abs(-v2));
    SHOW(abs(v1 - v2));
    SHOW(sizeof(v1));
  }
  {
    Vector4i v1(1, 2, 3, 4);
    v1.fill(5);
    SHOW(v1);
  }
  {
    Pixel pix(11, 0, 255, 1);
    SHOW(pix);
    SHOW(Vector4i(pix));
    SHOW(Vector4i(pix).pixel());
    SHOW((Vector4i(pix) + 15).pixel());
    SHOW((Vector4i(pix) + 250).pixel());
    SHOW((Vector4i(pix) + 100'000).pixel());
    SHOW((Vector4i(pix) + (std::numeric_limits<int>::max() - 255)).pixel());
    if (0) SHOW((Vector4i(pix) + std::numeric_limits<int>::max()).pixel());  // (overflows silently)
    SHOW((Vector4i(pix) - 1).pixel());
    SHOW((Vector4i(pix) - 15).pixel());
    SHOW((Vector4i(pix) - 100'000).pixel());
    SHOW((Vector4i(pix) - std::numeric_limits<int>::max()).pixel());
  }
  {
    Array<int> ar1{31, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 0};
    SHOW(ar1);
    Vector4i v;
    v.load_unaligned(ar1.data());
    SHOW(v);
    (v - 1).store_unaligned(ar1.data());
    SHOW(ar1);
  }
  {
    Vec4<int> ar1{31, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 0};
    SHOW(ar1);
    Vector4i v;
    v.load_unaligned(ar1.data());
    SHOW(v);
    (v - 1).store_unaligned(ar1.data());
    SHOW(ar1);
  }
}
