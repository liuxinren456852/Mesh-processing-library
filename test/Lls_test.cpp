// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Lls.h"

#include "libHh/MatrixOp.h"  // identity_mat()
#include "libHh/Random.h"
#include "libHh/SingularValueDecomposition.h"
#include "libHh/Stat.h"
using namespace hh;

namespace {

unique_ptr<Lls> make_lls(int c, int m, int n, int nd) {
  if (c == 0) return make_unique<SparseLls>(m, n, nd);
  if (c == 1) return make_unique<LudLls>(m, n, nd);
  if (c == 2) return make_unique<GivensLls>(m, n, nd);
  if (c == 3) return make_unique<SvdLls>(m, n, nd);
  if (c == 4) return make_unique<SvdDoubleLls>(m, n, nd);
  if (c == 5) return make_unique<QrdLls>(m, n, nd);
  assertnever("");
}

void test1() {
  {
    SvdLls lls(1, 1, 1);
    lls.enter_a_rc(0, 0, 1.f);
    lls.enter_b_rc(0, 0, 10.f);
    lls.enter_xest_rc(0, 0, 1.f);
    assertx(lls.solve());
    SHOW(lls.get_x_rc(0, 0));
  }
  {
    Vec1<float> X1 = {-100.f}, B1 = {2.f};
    Vec1<float> X2 = {-100.f}, B2 = {2.f};
    SvdLls lls(1, 1, 2);
    lls.enter_a_rc(0, 0, 4.f);
    lls.enter_b_c(0, B1);
    lls.enter_xest_c(0, X1);
    lls.enter_b_c(1, B2);
    lls.enter_xest_c(1, X2);
    assertx(lls.solve());
    lls.get_x_c(0, X1);
    lls.get_x_c(1, X2);
    SHOW(X1[0]);
    SHOW(X2[0]);
  }
  {
    float X1[1] = {-100.f};
    float X2[1] = {-100.f};
    SparseLls lls(1, 1, 2);
    lls.enter_a_rc(0, 0, 4.f);
    lls.enter_b_c(0, V(2.f));
    lls.enter_xest_c(0, X1);
    lls.enter_b_c(1, V(2.f));
    lls.enter_xest_c(1, X2);
    if (0) {
      lls.set_max_iter(200);
      lls.set_tolerance(1e-4f);
    }
    assertx(lls.solve());
    lls.get_x_c(0, X1);
    lls.get_x_c(1, X2);
    SHOW(X1[0]);
    SHOW(X2[0]);
    // SparseLls::Ival::pool.destroy();
  }
  {
    Vec1<float> X1 = {-100.f}, X2 = {-100.f};
    const Vec1<float> B1 = {2.f}, B2 = {2.f};
    LudLls lls(1, 1, 2);
    lls.enter_a_rc(0, 0, 4.f);
    lls.enter_b_c(0, B1);
    lls.enter_xest_c(0, X1);
    lls.enter_b_c(1, B2);
    lls.enter_xest_c(1, X2);
    assertx(lls.solve());
    lls.get_x_c(0, X1);
    lls.get_x_c(1, X2);
    SHOW(X1[0]);
    SHOW(X2[0]);
  }
  {
    const int n = 3;
    Matrix<float> a(n, n);
    Array<float> b(n);
    for_int(i, n) {
      for_int(j, n) {
        a[i][j] = 1.f + i * 3 + j + abs(2 - i) * abs(5 - j + i) * j + i * i * j * j;
        // showf("[%d, %d]=%g\n", i, j, a[i][j]);
      }
      b[i] = float(abs(i - 4));
      // SHOW(b[i]);
    }
    for_int(c, 6) {
      SHOW(c);
      const int nd = 2;
      auto up_lls = make_lls(c, n, n, nd);
      Lls& lls = *up_lls;
      for_int(i, n) {
        for_int(j, n) lls.enter_a_rc(i, j, a[i][j]);
        lls.enter_b_rc(i, 0, b[i]);
        lls.enter_b_rc(i, 1, b[i] * 2);
        lls.enter_xest_rc(i, 0, 0.f);
        lls.enter_xest_rc(i, 1, 0.f);
      }
      assertx(lls.solve());
      for_int(i, n) SHOW(round_fraction_digits(lls.get_x_rc(i, 0)));
      for_int(i, n) SHOW(round_fraction_digits(lls.get_x_rc(i, 1)));
    }
  }
}

void test2() {
  for_int(c, 6) {
    SHOW(c);
    auto up_lls = make_lls(c, 2, 1, 1);
    Lls& lls = *up_lls;
    lls.enter_a_rc(0, 0, 1.f);
    lls.enter_a_rc(1, 0, 1.f);
    lls.enter_b_rc(0, 0, 10.f);
    lls.enter_b_rc(1, 0, 20.f);
    lls.enter_xest_rc(0, 0, 50.f);
    assertx(lls.solve());
    SHOW(lls.get_x_rc(0, 0));
  }
}

void test3() {
  for_int(c, 6) {
    SHOW(c);
    auto up_lls = make_lls(c, 3, 2, 1);
    Lls& lls = *up_lls;
    lls.enter_a_rc(0, 0, 1.f);
    lls.enter_a_rc(0, 1, 1.f);
    lls.enter_a_rc(1, 0, 1.f);
    lls.enter_a_rc(1, 1, 0.f);
    lls.enter_a_rc(2, 0, 0.f);
    lls.enter_a_rc(2, 1, 1.f);
    lls.enter_b_rc(0, 0, 10.f);
    lls.enter_b_rc(1, 0, 2.f);
    lls.enter_b_rc(2, 0, 12.f);
    lls.enter_xest_rc(0, 0, 50.f);
    lls.enter_xest_rc(1, 0, 50.f);
    assertx(lls.solve());
    SHOW(round_fraction_digits(lls.get_x_rc(0, 0)));
    SHOW(round_fraction_digits(lls.get_x_rc(1, 0)));
  }
}

void test4() {
  using Real = float;
  for_int(imode, 2) {
    for_int(inormalize, 2) {
      for_intL(m, 1, 11) for_intL(n, 1, 11) {
        if (m < n) continue;
        Matrix<Real> A(m, n);
        switch (imode) {
          case 0:
            for (Real& v : A) v = possible_cast<Real>(Random::G.dunif());
            break;
          case 1: identity_mat(A); break;
          default: assertnever("");
        }
        if (inormalize) for_int(i, n) normalize(column(A, i));  // possibly normalize the columns
        if (1) {
          Matrix<Real> U(m, n);
          Array<Real> S(n);
          Matrix<Real> VT(n, n);
          bool success = singular_value_decomposition(A, U, S, VT);
          sort_singular_values(U, S, VT);
          Matrix<Real> R = mat_mul(mat_mul(U, diag_mat(S)), transpose(VT));
          Matrix<Real> UTU = mat_mul(transpose(U), U);
          Matrix<Real> VTV = mat_mul(transpose(VT), VT);
          float Rerr = Stat(R - A).max_abs();
          float UTUerr = Stat(UTU - identity_mat<Real>(n)).max_abs();
          float VTVerr = Stat(VTV - identity_mat<Real>(n)).max_abs();
          if (0 || !assertw(Rerr < 1e-6f && UTUerr < 1e-6f && VTVerr < 1e-6f))
            SHOW(inormalize, m, n, success, Rerr, UTUerr, VTVerr);
        }
      }
    }
  }
}

}  // namespace

int main() {
  test1();
  test2();
  test3();
  test4();
}
