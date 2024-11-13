// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_SET_H_
#define MESH_PROCESSING_LIBHH_SET_H_

#include <unordered_set>

#include "libHh/Random.h"

#if 0
{
  Set<Edge> sete;
  for (Edge e : set) consider(e);
  //
  struct mypair {
    unsigned _v1, _v2;
  };
  struct hash_mypair {
    size_t operator()(const mypair& e) const { return hash_combine(my_hash(e._v1), e._v2); }
  };
  struct equal_mypair {
    bool operator()(const mypair& e1, const mypair& e2) const { return e1._v1 == e2._v1 && e1._v2 == e2._v2; }
  };
  Set<mypair, hash_mypair, equal_mypair> setpairs;
  //
  struct hash_edge {  // From MeshOp.cpp.
    ...;
    const GMesh& _mesh;
  };
  hash_edge he(mesh);
  Set<Edge, hash_edge> sete(he);
}
#endif

namespace hh {

// My wrapper around std::unordered_set<>.  (typename Equal also goes by name Pred in C++ standard library).
template <typename T, typename Hash = std::hash<T>, typename Equal = std::equal_to<T>> class Set {
  using type = Set<T, Hash, Equal>;
  using base = std::unordered_set<T, Hash, Equal>;

 public:
  using Hashf = typename base::hasher;
  using Equalf = typename base::key_equal;
  using value_type = T;
  using iterator = typename base::iterator;
  using const_iterator = typename base::const_iterator;
  Set() = default;
  explicit Set(Hashf hashf) : _set(0, hashf) {}
  explicit Set(Hashf hashf, Equalf equalf) : _set(0, hashf, equalf) {}
  Set(std::initializer_list<T> list) : _set(std::move(list)) {}
  void clear() { _set.clear(); }
  void enter(const T& e) {  // Element e must be new.
    auto [_, is_new] = _set.insert(e);
    ASSERTX(is_new);
  }
  void enter(T&& e) {  // Element e must be new.
    auto [_, is_new] = _set.insert(std::move(e));
    ASSERTX(is_new);
  }
  const T& enter(const T& e, bool& is_new) {
    auto [it, is_new_] = _set.insert(e);
    is_new = is_new_;
    return *it;
  }
  // Omit "const T& enter(T&& e, bool& is_new)" because e could be lost if !is_new.
  bool add(const T& e) {  // Return: is_new.
    auto [_, is_new] = _set.insert(e);
    return is_new;
  }
  // Omit "bool add(T&& e)" because e could be lost if !is_new.
  bool remove(const T& e) { return remove_i(e); }  // Return: was_found.
  bool contains(const T& e) const { return _set.find(e) != end(); }
  int num() const { return narrow_cast<int>(_set.size()); }
  size_t size() const { return _set.size(); }
  bool empty() const { return _set.empty(); }
  const T& retrieve(const T& e, bool& present) const { return retrieve_i(e, present); }
  const T& retrieve(const T& e) const {
    auto it = _set.find(e);
    return it != end() ? *it : def();
  }
  const T& get(const T& e) const {
    auto it = _set.find(e);
    ASSERTXX(it != end());
    return *it;
  }
  const T& get_one() const { return (ASSERTXX(!empty()), *begin()); }
  const T& get_random(Random& r) const {
    auto it = crand(r);
    return *it;
  }
  T remove_one() {
    ASSERTXX(!empty());
    // T e = std::move(*begin());  // See discussion in Set_test.cpp.
    // _set.erase(begin());
    auto node = _set.extract(begin());
    return std::move(node.value());
  }
  T remove_random(Random& r) {
    auto it = crand(r);
    auto node = _set.extract(it);
    return std::move(node.value());
  }
  iterator begin() { return _set.begin(); }
  const_iterator begin() const { return _set.begin(); }
  iterator end() { return _set.end(); }
  const_iterator end() const { return _set.end(); }
  void merge(type& other) { _set.merge(other._set); }  // Elements are moved from `other` if not already in *this.

 private:
  base _set;
  static const T& def() {
    static const T k_default = T{};
    return k_default;
  }
  const T& retrieve_i(const T& e, bool& present) const {
    auto it = _set.find(e);
    present = it != end();
    return present ? *it : def();
  }
  bool remove_i(const T& e) {
    if (_set.erase(e) == 0) return false;
    if (1 && _set.size() < _set.bucket_count() / 16) _set.rehash(0);
    return true;
  }
  const_iterator crand(Random& r) const {  // See also similar code in Map.
    assertx(!empty());
    if (0) {
      return std::next(begin(), r.get_size_t() % _set.size());  // Likely slow; no improvement.
    } else {
      size_t nbuckets = _set.bucket_count();
      size_t bn = r.get_size_t() % nbuckets;
      size_t ne = _set.bucket_size(bn);
      size_t nskip = r.get_size_t() % (20 + ne);
      while (nskip >= _set.bucket_size(bn)) {
        nskip -= _set.bucket_size(bn);
        bn++;
        if (bn == nbuckets) bn = 0;
      }
      auto li = _set.begin(bn);
      while (nskip--) {
        ASSERTXX(li != _set.end(bn));
        ++li;
      }
      ASSERTXX(li != _set.end(bn));
      // Convert from const_local_iterator to const_iterator.
      auto it = _set.find(*li);
      ASSERTXX(it != _set.end());
      return it;
    }
  }
  // Default operator=() and copy_constructor are safe.
};

template <typename T> HH_DECLARE_OSTREAM_RANGE(Set<T>);
template <typename T> HH_DECLARE_OSTREAM_EOL(Set<T>);

}  // namespace hh

#endif  // MESH_PROCESSING_LIBHH_SET_H_
