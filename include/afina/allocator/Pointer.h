#ifndef AFINA_ALLOCATOR_POINTER_H
#define AFINA_ALLOCATOR_POINTER_H

namespace Afina {
namespace Allocator {
// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Simple;

class Pointer {
  friend class Simple;
public:
    Pointer();
    explicit Pointer(void *descr);

    Pointer(const Pointer &other);
    Pointer(Pointer &&other);

    Pointer &operator=(const Pointer &rhs);
    Pointer &operator=(Pointer &&rhs);

    void *get() const {
      if (m_descr == nullptr)
        return nullptr;
      return *(void **)(m_descr); }

    void *m_descr;
};

} // namespace Allocator
} // namespace Afina

#endif // AFINA_ALLOCATOR_POINTER_H
