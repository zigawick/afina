#include <afina/allocator/Simple.h>

#include <afina/allocator/Error.h>
#include <afina/allocator/Pointer.h>
#include <vector>
#include <iostream>
#include <stdio.h>
#include <string.h>


namespace Afina
{
  namespace Allocator
  {


/// get offset and capacity
    std::pair < size_t, size_t > Simple::get_info (size_t offset)
    {
      std::pair < size_t, size_t > ret;

      ret.first = get_val (offset);
      ret.second = get_val (offset + size_t_size);
      return ret;
    }

    void Simple::put_val (size_t offset, size_t value)
    {
      *static_cast < size_t * >(_base + offset) = value;
    }

    size_t Simple::get_val (size_t offset) const
    {
      return *(size_t *) (_base + offset);
    }

// get descr and is new flag
// if first is m_first_free_descr it means we made defragmentation and allocation have to be restarted
    std::pair < size_t, bool > Simple::get_new_descriptor ()
    {
      size_t free_descr = get_val (m_first_free_descr);

      if (free_descr == 0)	// we have no free descriptors
    {
      size_t descr_size = get_val (m_descr_table_size);
      size_t free = get_val (m_free_space_count);
      if (free < size_t_size * 2)
        //check if we have enough free space for new descriptor
        {
          throw Allocator::AllocError (AllocErrorType::NoMemory,
                       "not enough memory for new descriptor");
        }
      size_t prev_free_offset = _base_len - size_t_size;
      size_t free_offset = get_val (prev_free_offset);

      while (free_offset != m_first_free_block)
      {
          prev_free_offset = free_offset;
          free_offset = get_val (prev_free_offset);
      }

      auto free_info = get_info(prev_free_offset);
      size_t end_of_free = prev_free_offset + free_info.second;
      if (end_of_free != _base_len - 4 * size_t_size - 2 * size_t_size * descr_size)
      {
          defrag();
          return  {m_first_free_descr, false};
      }



      descr_size += 1;
      put_val (m_descr_table_size, descr_size);
      put_val (m_free_space_count,
           get_val (m_free_space_count) - 2 * size_t_size);
      put_val(prev_free_offset + 1 * size_t_size, free_info.second - 2 * size_t_size);

      return
      {
      _base_len - 2 * descr_size * size_t_size - 4 * size_t_size, true};
    }
      else
    {
      put_val (_base_len - 3 * size_t_size, get_val (free_descr));
      return
      {
      free_descr, false};
    }
    }

  Simple::Simple (void *base, size_t size):_base (base), _base_len (size)
    {
      memset (_base, 0, _base_len);
      m_first_free_block = _base_len - 1 * size_t_size;
      m_descr_table_size = _base_len - 2 * size_t_size;
      m_first_free_descr = _base_len - 3 * size_t_size;
      m_free_space_count = _base_len - 4 * size_t_size;

      put_val (0, m_first_free_block);	// offset to the next free block
      put_val (1 * size_t_size, _base_len - 4 * size_t_size);	// size of free block
      put_val (m_first_free_block, 0);	// offset to first free block
      put_val (m_descr_table_size, 0);	// size of descriptors table
      put_val (m_first_free_descr, 0);	// offset to the first free descriptor
      put_val (m_free_space_count, _base_len - 4 * size_t_size);	// free space count
    }

/**
 * TODO: semantics
 * @param N size_t
 */
    Pointer Simple::alloc (size_t N)
    {
      size_t capacity = get_val (m_free_space_count);

      if (capacity < N + 2 * size_t_size)
    {
      throw AllocError (AllocErrorType::NoMemory, "");
    }


      size_t prev_free_offset = _base_len - size_t_size;
      size_t free_offset = get_val (prev_free_offset);

      std::pair < size_t, size_t > free_info;
      // looking for first large enough part
      do
    {
      auto free_info = get_info (free_offset);
      if (free_info.second >= N)
        {
          auto descr_offset = get_new_descriptor ();

          //reeeestart!
          if (descr_offset.first == m_first_free_descr)
          {
              return alloc(N);
          }

          if (descr_offset.second && free_info.first == m_first_free_block)
          {
              // free descr and rerun alloc
          }

          put_val (descr_offset.first, (size_t) (_base + free_offset));
          auto ret_ptr = Pointer (_base + descr_offset.first);
          size_t alloc_mem = 0;

          // can we leave free space in this sector?
          if (free_info.second - N >= 2 * size_t_size)
        {
          put_val (descr_offset.first + size_t_size, N);

          put_val (free_offset + N, free_info.first);	// new freespace offset
          put_val (free_offset + N + size_t_size, free_info.second - N);	// new freespace size
          put_val (prev_free_offset, free_offset + N);	// offset to new free space
          alloc_mem = N;
        }
          else		// no we can't. So return whole sector
        {
          put_val (descr_offset.first + size_t_size,
               free_info.second);
          put_val (free_offset, free_info.first);	// offset to new free space
          alloc_mem = free_info.second;
        }

          put_val (m_free_space_count,
               get_val (m_free_space_count) - alloc_mem);
//          std::cout << "alloc\n";
          dump ();
          return ret_ptr;
        }

      capacity += free_info.second;
      free_offset = get_val (free_info.first);
    }
      while (free_info.first != 0);

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
    void Simple::realloc (Pointer & p, size_t N)
    {
      free (p);
      p = alloc (N);
    }

/**
 * TODO: semantics
 * @param p Pointer
 */
    void Simple::free (Pointer & p)
    {
      void *descr = p.m_descr;
      if (descr == 0)
    return;
      if (_base > descr)
    return;

      size_t descr_offset = (size_t) descr - (size_t) _base;
      auto descr_info = get_info (descr_offset);
      descr_info.first = descr_info.first - (size_t)_base;

      size_t prev_free_space = get_val (m_free_space_count);	// store prev free space to do defrag maybe
      put_val (m_free_space_count,
           get_val (m_free_space_count) + descr_info.second);

      // deal with free space
      if (descr_info.second != 0)
    {
      size_t end_of_part =
        get_val (descr_info.first) + descr_info.second;// TODO there was +1
      size_t neighbor = m_first_free_block;
      size_t temp = get_val (neighbor);
      while (temp != descr_info.first && temp != end_of_part && temp != m_first_free_block)
        {
          neighbor = temp;
          temp = get_val (temp);
        }

      // we're creating new free pice
      if (temp == end_of_part)
        {
          put_val (neighbor, descr_info.first);
          put_val (descr_info.first, get_val (temp));	// pointer
          put_val (descr_info.first + size_t_size,	//size
               descr_info.second + get_val (temp + size_t_size));
        }
      else if (temp == descr_info.first)	// we're uniting two pices free + this
        {
          if (get_val (temp) == end_of_part)	// free + this + free
        {
          put_val (neighbor, get_val (temp));
          put_val (neighbor + 1 * size_t_size,
               get_val (neighbor + 1 * size_t_size) +
               descr_info.second + get_val (temp +
                            1 * size_t_size));
        }
          else
        {
          put_val (neighbor + 1 * size_t_size,
               get_val (neighbor + 1 * size_t_size) +
               descr_info.second);
        }
        }
      else			// we're uniting two pices this + free
        {
          put_val (neighbor, descr_info.first);
          put_val (descr_info.first, temp);	// pointer
          put_val (descr_info.first + size_t_size, descr_info.second);	// size
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

      put_val (last_free_descr_offset, descr_offset);
      put_val (descr_offset, 0);
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


// returns is in pair, prev, next
    bool Simple::is_in_free (size_t offset)
    {
      size_t neighbor = m_first_free_block;
      size_t temp = get_val (neighbor);
      while (temp != offset && temp != m_first_free_block)
    {
      neighbor = temp;
      temp = get_val (temp);
    }
      if (temp == m_first_free_block)
    {
      return false;
    }

      return true;
    }


    size_t Simple::find_descriptor_for_memory (size_t point)
    {
      size_t first_descr =
    _base_len - get_val (m_descr_table_size) * 2 - 4 * size_t_size;
      size_t last_descr = _base_len - 4 * size_t_size;
      void *pointer = _base + point;

      for (size_t it = first_descr; it < last_descr; it += 2 * size_t_size)
    {
      if (pointer == (void *) get_val (it))
        return it;
    }

      throw AllocError (AllocErrorType::InternalError, "");
    }

    void Simple::move_by_byte (size_t descr, size_t point)
    {
      auto info = get_info (descr);

      for (size_t i = 0; info.second > i; i++)
    {
      memcpy ((void *) _base + point + i, (void *) info.first + i, 1);
    }

    }

    void Simple::defrag ()
    {
      size_t free_offset = m_first_free_block;
      while (free_offset != m_first_free_block)
    {


      auto free_info = get_info (free_offset);

      size_t point = free_info.first;

      size_t descr = find_descriptor_for_memory (point);
      auto descr_info = get_info (descr);


      if (descr_info.second > free_info.second)	// moving byte by byte
        {
          move_by_byte (descr, point);
        }
      else
        {
          memcpy (_base + free_info.first, (void *) descr_info.first,
              descr_info.second);
        }

      put_val (descr, (size_t) (_base + free_info.first));

      size_t block_end =
        (size_t) ((void *) descr_info.first - (size_t) _base) +
        descr_info.second;
      size_t new_block_end = free_offset + descr_info.second;

      put_val (m_first_free_block, new_block_end);
      // update free sector
      if (free_info.first == block_end)	// we have freee space after block
        {
          auto next_free = get_info (block_end);
          put_val (new_block_end, next_free.first);
          put_val (new_block_end + size_t_size,
               free_info.second + next_free.second);
        }
      else
        {
          put_val (new_block_end, free_info.first);
          put_val (new_block_end + size_t_size, free_info.second);
        }
    }



      std::cout << "defrag\n";
    }

/**
 * TODO: semantics
 */
    std::vector<size_t> Simple::dump ()const
    {
      std::vector<size_t> v;
  for (size_t i = 0 ; i < _base_len / size_t_size; i++)
    {
      v.push_back (get_val (i * size_t_size) / size_t_size);
    }
      return v;
    }

  }				// namespace Allocator
}				// namespace Afina
