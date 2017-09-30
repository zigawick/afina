#include <afina/allocator/Pointer.h>

namespace Afina
{
  namespace Allocator
  {

    Pointer::Pointer ()
    {
    }

    Pointer::Pointer (void *descr)
    {
      m_descr = descr;
    }
    Pointer::Pointer (const Pointer & other)
    {
      m_descr = other.m_descr;
    }
    Pointer::Pointer (Pointer && other)
    {
      m_descr = other.m_descr;
    }

    Pointer & Pointer::operator= (const Pointer & rhs)
    {
      m_descr = rhs.m_descr;
      return *this;
    }
    Pointer & Pointer::operator= (Pointer && rhs)
    {
      m_descr = rhs.m_descr;
      return *this;
    }

  }				// namespace Allocator
}				// namespace Afina
