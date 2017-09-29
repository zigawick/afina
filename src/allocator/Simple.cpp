#include <afina/allocator/Simple.h>

#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>
#include <iostream>
#include <stdio.h>
#include <string.h>


namespace Afina {
namespace Allocator {


/// get offset and capacity
std::pair<size_t, size_t> Simple::get_info (size_t offset)
{
  std::pair<size_t, size_t> ret;

  ret.first = get_val (offset);
  ret.second = get_val (offset + size_t_size);
  return ret;
}

size_t * Simple::get_ptr (size_t offset)
{
  return (size_t *) (_base + offset);
}

size_t Simple::get_val (size_t offset) const
{
  return *(size_t *) (_base + offset);
}

// get descr and is new flag
std::pair<size_t, bool> Simple::get_new_descriptor ()
{
  size_t free_descr = get_val(m_first_free_descr);

  if (free_descr == 0) // we have no free descriptors
    {
      size_t *descr_size = get_ptr (m_descr_table_size );
      size_t free = get_val (m_free_space_count);
      if (free == 0)
        //check if we have enough free space for new descriptor
        {
          throw Allocator::AllocError (AllocErrorType::NoMemory, "not enough memory for new descriptor");
        }
      *descr_size += 1;
      *get_ptr (m_free_space_count) -= 2 * size_t_size;
      return {_base_len - 2 * (*descr_size) * size_t_size - 4 * size_t_size, true};
    }
  else
    {
      *get_ptr (_base_len- 3 * size_t_size) = *get_ptr (free_descr);
      return {free_descr, false};
    }
}

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
  memset (_base, 0, _base_len);
  m_first_free_block = _base_len - 1 * size_t_size;
  m_descr_table_size = _base_len - 2 * size_t_size;
  m_first_free_descr = _base_len - 3 * size_t_size;
  m_free_space_count = _base_len - 4 * size_t_size;

  *get_ptr (0) = 0;// offset to the next free block
  *get_ptr (1 * size_t_size) = _base_len - 4 * size_t_size; // size of free block
  *get_ptr (m_first_free_block) = 0; // offset to first free block
  *get_ptr (m_descr_table_size ) = 0; // size of descriptors table
  *get_ptr (m_first_free_descr) = 0; // offset to the first free descriptor
  *get_ptr (m_free_space_count) = _base_len - 4 * size_t_size; // free space count
}

/**
 * TODO: semantics
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) {
  size_t capacity = get_val (m_free_space_count);

  if (capacity < N + 2 * size_t_size)
    {
      throw AllocError (AllocErrorType::NoMemory, "");
    }


  size_t prev_free_offset = _base_len - size_t_size;
  size_t *free_offset = get_ptr (prev_free_offset);

  std::pair<size_t, size_t> free_info;
  // looking for first large enough part
  do
    {
      auto free_info = get_info (*free_offset);
      if (free_info.second >= N)
        {
          auto descr_offset = get_new_descriptor ();
          if (descr_offset.second)
            {
              // if last pice
              if (free_info.first + free_info.second >
                  _base_len - 4 * size_t_size - 2 * size_t_size * get_val (m_descr_table_size))
                {
                  // place for new created descriptors
                  free_info.second -= _base_len - 4 * size_t_size - 2 * size_t_size * get_val (m_descr_table_size)
                                      - free_info.first;
                }
            }
          (*get_ptr(descr_offset.first)) = (size_t) (_base + *free_offset);
          auto ret_ptr = Pointer (_base + descr_offset.first);
          size_t alloc_mem = 0;

          // can we leave free space in this sector?
          if (free_info.second - N >= 2 * size_t_size)
            {
              *get_ptr (descr_offset.first + size_t_size) = N;

              *get_ptr (*free_offset + N) = free_info.first ;// new freespace offset
              *get_ptr (*free_offset + N + size_t_size) = free_info.second - N;// new freespace size
              *get_ptr (prev_free_offset) = *free_offset + N; // offset to new free space
              alloc_mem = N;
            }
          else // no we can't. So return whole sector
            {
              *get_ptr (descr_offset.first + size_t_size) = free_info.second;
              *free_offset = free_info.first; // offset to new free space
              alloc_mem = free_info.second;
            }

          *get_ptr (m_free_space_count) -= alloc_mem;
//          std::cout << "alloc\n";
          dump ();
          return ret_ptr;
        }

      capacity += free_info.second;
      free_offset = get_ptr (free_info.first);
    }while (free_info.first != 0);

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
void Simple::realloc(Pointer &p, size_t N) {
  free (p);
  p = alloc (N);
}

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

  size_t prev_free_space = get_val (m_free_space_count);// store prev free space to do defrag maybe
  *get_ptr (m_free_space_count) += descr_info.second;

  // deal with free space
  if (descr_info.second != 0)
    {
      size_t end_of_part = get_val (descr_info.first) + descr_info.second + 1;
      size_t neighbor = m_first_free_block;
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
          *get_ptr (descr_info.first + size_t_size) = descr_info.second; // size
        }
      else // we're uniting two pices
        {
          *get_ptr (neighbor) = descr_info.first;
          *get_ptr (descr_info.first) = get_val (temp); // pointer
          *get_ptr (descr_info.first + size_t_size) //size
              = descr_info.second + get_val (temp + size_t_size);
        }
    }

  // deal with descriptor
  size_t last_free_descr_offset = m_first_free_descr;
  size_t temp = get_val (m_first_free_descr);

  while (temp != 0)
    {
      last_free_descr_offset = temp;
      temp = get_val (temp);
    }

  *get_ptr (last_free_descr_offset) = descr_offset;
  *get_ptr (descr_offset) = 0;
  p.m_descr = 0;

  if (prev_free_space == 0)
    {
      defrag ();
    }

  std::cout << "free\n";
  dump ();
}

/**
 * TODO: semantics
 */
void Simple::defrag() {
  std::cout<<"defrag\n";
}

/**
 * TODO: semantics
 */
std::string Simple::dump() const {
  std::string d;
//  for (size_t i = 0 ; i < _base_len / size_t_size + 1; i++)
//    {
//      d += " " + std::to_string(get_val (i));
//    }
//  std::cout<<d;
  return d;
}

} // namespace Allocator
} // namespace Afina
