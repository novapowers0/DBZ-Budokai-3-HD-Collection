#pragma once
#include <cstdint>

namespace rex {

class Runtime {
 public:
  static Runtime*& InstancePtr() {
    static Runtime* p = nullptr;
    return p;
  }
  static Runtime* instance() { return InstancePtr(); }
  uint8_t* virtual_membase() const { return base_; }
  void set_virtual_membase(uint8_t* b) { base_ = b; }

 private:
  uint8_t* base_ = nullptr;
};

}  // namespace rex
