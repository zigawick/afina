#include <afina/allocator/Simple.h>

#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

constexpr size_t size_t_size ()
{
  return sizeof (size_t);
}

/// get offset and capacity
std::pair<size_t, size_t> Simple::get_info (size_t offset)
{
  std::pair<size_t, size_t> ret;

  ret.first = get_val (offset);
  ret.second = get_val (offset + size_t_size ());
  return ret;
}

size_t * Simple::get_ptr (size_t offset)
{
  return (size_t *) (_base + offset);
}

size_t Simple::get_val (size_t offset)
{
  return *(size_t *) (_base + offset);
}


size_t Simple::get_new_descriptor ()
{
  size_t free_descr = get_val(_base_len - 3 * size_t_size ());

  if (free_descr == 0) // we have no free descriptors
    {
      size_t *descr_size = get_ptr (_base_len - 2 * size_t_size ());
      //TODO: check if we have enough space for new descriptor
      *descr_size += 1;
      return _base_len - 2 * (*descr_size) * size_t_size () - 3 * size_t_size ();
    }
  else
    {
      *get_ptr (_base_len- 3 * size_t_size ()) = *get_ptr (free_descr);
      return free_descr;
    }
}

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
  *get_ptr (0) = 0;// offset to the next free block
  *get_ptr (1 * size_t_size ()) = size - 3 * size_t_size (); // size of free block
  *get_ptr (_base_len - 1 * size_t_size ()) = 0; // offset to first free block
  *get_ptr (_base_len - 2 * size_t_size ()) = 0; // size of descriptors table
  *get_ptr (_base_len - 3 * size_t_size ()) = 0; // offset to the first free descriptor
}

/**
 * TODO: semantics
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) {
  size_t capacity = 0;
  size_t *free_offset = get_ptr (_base_len - size_t_size ());

  std::pair<size_t, size_t> free_info;
  // looking for first large enough part
  do
    {
      auto free_info = get_info (*free_offset);
      if (free_info.second >= N)
        {
          size_t descr_offset = get_new_descriptor ();
          (*get_ptr(descr_offset)) = (size_t) (_base + *free_offset);
          auto ret_ptr = Pointer (_base + descr_offset);

          // can we leave free space in this sector?
          if (free_info.second - N >= 2 * size_t_size ())
            {
              *get_ptr (descr_offset + size_t_size ()) = N;

              *free_offset = descr_offset + N; // offset to new free space
              *get_ptr (*free_offset) = free_info.first ;// new freespace offset
              *get_ptr (*free_offset + size_t_size ()) = free_info.second - N;// new freespace size
            }
          else // no we can't. So return whole sector
            {
              *get_ptr (descr_offset + size_t_size ()) = free_info.second;
              *free_offset = free_info.first; // offset to new free space
            }

          return ret_ptr;
        }

      capacity += free_info.second;
      free_offset = get_ptr (free_info.first);
    }while (free_info.first != 0);

  // we have not enough memory
  if (capacity < N)
    {
      AllocError (AllocErrorType::NoMemory, "Not enough memory");
    }

  // looks like we have to do defragmentation
  defrag ();
  // now we have free space to alloc
  return alloc (N);
}

/**
 * TODO: semantics
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N) {}

/**
 * TODO: semantics
 * @param p Pointer
 */
void Simple::free(Pointer &p) {
  void *descr = p.m_descr;
  if (descr == 0)
    return;
  if (_base < descr)
    return ;

  size_t descr_offset = (size_t)_base - (size_t)descr;
  auto descr_info = get_info (descr_offset);

  // deal with free space
  if (descr_info.second != 0)
    {
      size_t end_of_part = get_val (descr_info.first) + descr_info.second + 1;
      size_t neighbor = _base_len - 1 * size_t_size ();
      size_t temp = get_val (neighbor);
      while (temp != end_of_part && temp != 0)
        {
          neighbor = temp;
          temp = get_val (temp);
        }

      // we're creating new free pice
      if (temp != end_of_part)
        {
          *get_ptr (neighbor) = descr_info.first;
          *get_ptr (descr_info.first) = temp; // pointer
          *get_ptr (descr_info.first + size_t_size ()) = descr_info.second; // size
        }
      else // we're uniting two pices
        {
          *get_ptr (neighbor) = descr_info.first;
          *get_ptr (descr_info.first) = get_val (temp); // pointer
          *get_ptr (descr_info.first + size_t_size ()) //size
              = descr_info.second + get_val (temp + size_t_size ());
        }
    }

  // deal with descriptor
  size_t last_free_descr_offset = _base_len - 3 * size_t_size ();
  size_t temp = get_val (_base_len - 3 * size_t_size ());

  while (temp != 0)
    {
      last_free_descr_offset = temp;
      temp = get_val (temp);
    }

  *get_ptr (last_free_descr_offset) = descr_offset;
  *get_ptr (descr_offset) = 0;
  p.m_descr = 0;
}

/**
 * TODO: semantics
 */
void Simple::defrag() {}

/**
 * TODO: semantics
 */
std::string Simple::dump() const { return ""; }

} // namespace Allocator
} // namespace Afina
