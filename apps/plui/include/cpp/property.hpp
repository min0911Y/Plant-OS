#pragma once
#include <type.hpp>

template <typename T, T (*GET)()>
class Getter {
public:
  Getter() = default;

  operator T() {
    return cpp::move(GET());
  }
};

template <typename T, void (*SET)(const T &)>
class Setter {
public:
  Setter() = default;

  auto operator=(const T &val) -> Setter & {
    SET(val);
    return *this;
  }
};

template <typename T, T (*GET)(), void (*SET)(const T &)>
class Property {
public:
  Property() = default;

  auto operator=(const T &val) -> Property & {
    SET(val);
    return *this;
  }

  operator T() {
    return cpp::move(GET());
  }
};

template <typename T, const T &(*GET)(const T &), void (*SET)(T &, const T &)>
class Checked {
private:
  T value;

public:
  Checked() = default;

  auto operator=(const T &val) -> Checked & {
    SET(value, val);
    return *this;
  }

  operator T &() {
    return GET(value);
  }
};
