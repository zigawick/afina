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
    *reinterpret_cast<size_t * >((char *)_base + offset) = value;
}

size_t Simple::get_val (size_t offset) const
{
    return *(size_t *) ((char *)_base + offset);
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

    if ( N < 2 * size_t_size)
        N = 2 * size_t_size;
    size_t capacity = get_val (m_free_space_count);

    if (capacity < N + 2 * size_t_size)
    {
        throw AllocError (AllocErrorType::NoMemory, "");
    }


    size_t prev_free_offset = _base_len - size_t_size;
    size_t free_offset = get_val (prev_free_offset);

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
                // we've updated index so rerun alloc
            {
                size_t free_descr_last = m_first_free_descr;
                while (get_val (free_descr_last) != 0)
                {
                    free_descr_last = get_val (free_descr_last);
                }

                put_val (free_descr_last, descr_offset.first);
                return alloc (N);
            }

            put_val (descr_offset.first, (size_t) ((char *)_base + free_offset));
            auto ret_ptr = Pointer ((char *)_base + descr_offset.first);
            size_t alloc_mem = 0;

            // can we leave free space in this sector?
            if (free_info.second - N >= 2 * size_t_size)
            {
                put_val (descr_offset.first + size_t_size, N);

                put_val (free_offset + N + size_t_size, get_val (free_offset + 1 * size_t_size) - N);	// new freespace size
                put_val (free_offset + N, free_info.first);	// new freespace offset
                put_val (prev_free_offset, free_offset + N);	// offset to new free space
                alloc_mem = N;
            }
            else		// no we can't. So return whole sector
            {
                put_val (descr_offset.first + size_t_size,
                         free_info.second);
                put_val (prev_free_offset, free_info.first);	// offset to new free space
                alloc_mem = free_info.second;
            }

            put_val (m_free_space_count,
                     get_val (m_free_space_count) - alloc_mem);
            //          std::cout << "alloc\n";
            dump ();
            return ret_ptr;
        }

        capacity += free_info.second;
        free_offset = free_info.first;
    }
    while (free_offset != m_first_free_block);

    // looks like we have to do defragmentation
    throw AllocError (AllocErrorType::NoMemory, "need defrag");
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
    //TODO add check
    void *descr = p.m_descr;

    if (descr == nullptr)
    {
        p = alloc (N);
        return;
    }

    size_t descr_offset = (size_t) descr - (size_t) _base;
    auto descr_info = get_info (descr_offset);
    descr_info.first = descr_info.first - (size_t)_base;

    size_t end_of_part = descr_info.first + descr_info.second;
    size_t prev_free_piece = m_first_free_block;
    size_t next_free_piece = m_first_free_block;
    get_prev_next_free(end_of_part, prev_free_piece, descr_info, next_free_piece);


    if (descr_info.second >= N)// make it smaller
    {
        if (descr_info.second - N < 2 * size_t_size)//not enough for new free pice
            return;

        put_val(descr_offset + 1 * size_t_size, N);
        if (next_free_piece == end_of_part) // we're uniting two pices this + free
        {
            put_val (descr_info.first + N, get_val(next_free_piece));	// pointer
            put_val (descr_info.first + N + size_t_size, descr_info.second - N + get_val (next_free_piece + 1  * size_t_size));	// size
        }
        else         // we're creating new free pice
        {
            put_val (prev_free_piece, descr_info.first + N);
            put_val (descr_info.first + N, next_free_piece);	// pointer
            put_val (descr_info.first + N + size_t_size, descr_info.second - N); // size
        }

        put_val (m_free_space_count, get_val(m_free_space_count) + descr_info.second - N);
        return;
    }


    if (N - descr_info.second > get_val(m_free_space_count))
        throw AllocError (AllocErrorType::NoMemory, "");

    auto next_free_info = get_info(next_free_piece);
    if (next_free_piece != end_of_part || N > next_free_info.second + descr_info.second)// move allocated memory
    {
        defrag();// reset old values
        descr_offset = (size_t) descr - (size_t) _base;
        descr_info = get_info (descr_offset);
        descr_info.first = descr_info.first - (size_t)_base;
        next_free_piece = get_val (m_first_free_block);
        next_free_info = get_info(next_free_piece);
        if (next_free_info.second < N) // sorry so hard logic to implement
            throw AllocError (AllocErrorType::NoMemory, "");

        memmove ((char *)_base + next_free_piece, (char *)_base + descr_info.first, descr_info.second);
        put_val (descr_offset, (size_t) ((char *)_base + next_free_piece));

        put_val (m_first_free_block, descr_info.first);


        prev_free_piece = descr_info.first;
        put_val (descr_info.first, next_free_piece + descr_info.second);
        put_val (descr_info.first + 1 * size_t_size, descr_info.second);


        next_free_piece = next_free_piece + descr_info.second;
        put_val (next_free_piece, m_first_free_block);
        put_val (next_free_piece + 1  * size_t_size, next_free_info.second - descr_info.second);

        next_free_info = get_info (next_free_piece);
        descr_info = get_info (descr_offset);
        descr_info.first = descr_info.first - (size_t)_base;
    }

    if (next_free_info.second - (N - descr_info.second) < 2 * size_t_size) //take all piece
    {
        N = next_free_info.second + descr_info.second;
    }

    if (N == next_free_info.second + descr_info.second) // remove free pice
    {
        put_val(descr_offset + 1 * size_t_size, N);
        put_val (prev_free_piece, next_free_info.first);	// offset to new free space
    }
    else
    {
        put_val (prev_free_piece, descr_info.first + N);
        put_val (descr_info.first + N, next_free_info.first);	// pointer
        put_val (descr_info.first + N + size_t_size, next_free_info.second - N + descr_info.second); // size
    }
    put_val (m_free_space_count, get_val(m_free_space_count) + descr_info.second - N);
    return;
}

/**
 * TODO: semantics
 * @param p Pointer
 */
void Simple::get_prev_next_free(size_t end_of_part, size_t &prev_free_piece, std::pair<size_t, size_t> descr_info, size_t &next_free_piece)
{
    bool curr_is_set = false;
    size_t prev = m_first_free_block;
    size_t curr = get_val (prev);
    while (curr != m_first_free_block)
    {
        if (prev < descr_info.first)
            prev_free_piece = prev;
        if (curr >= end_of_part && !curr_is_set)
        {
            curr_is_set = true;
            next_free_piece = curr;
        }
        prev = curr;
        curr = get_val (prev);
    }
}

void Simple::unit_free_picies(size_t end_of_part, size_t prev_free_piece, std::pair<size_t, size_t> descr_info, size_t next_free_piece, bool from_realloc)
{
    if (!from_realloc && prev_free_piece + get_val (prev_free_piece) == descr_info.first)	// we're uniting two pices free + this
    {
        if (next_free_piece == end_of_part)	// free + this + free
        {
            put_val (prev_free_piece, get_val (next_free_piece));
            put_val (prev_free_piece + 1 * size_t_size,
                     get_val (next_free_piece + 1 * size_t_size) +
                     get_val (prev_free_piece + 1 * size_t_size) +
                     descr_info.second);
        }
        else // free + this
        {
            put_val (prev_free_piece + 1 * size_t_size,
                     get_val (prev_free_piece + 1 * size_t_size) +
                     descr_info.second);
        }
    }
    else if (next_free_piece == end_of_part) // we're uniting two pices this + free
    {
        put_val (descr_info.first, get_val(next_free_piece));	// pointer
        put_val (descr_info.first + size_t_size, descr_info.second + get_val (next_free_piece + 1  * size_t_size));	// size
    }
    else         // we're creating new free pice
    {
        put_val (prev_free_piece, descr_info.first);
        put_val (descr_info.first, next_free_piece);	// pointer
        put_val (descr_info.first + size_t_size, descr_info.second); // size
    }
}

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
        size_t end_of_part = descr_info.first + descr_info.second;
        size_t prev_free_piece = m_first_free_block;
        size_t next_free_piece = m_first_free_block;
        get_prev_next_free(end_of_part, prev_free_piece, descr_info, next_free_piece);
        unit_free_picies(end_of_part, prev_free_piece, descr_info, next_free_piece);
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

    //    std::cout << "free\n";
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
            _base_len - get_val (m_descr_table_size) * 2 * size_t_size - 4 * size_t_size;
    size_t last_descr = _base_len - 4 * size_t_size;
    void *pointer = (void *)((char *)_base + point);

    for (size_t it = first_descr; it < last_descr; it += 2 * size_t_size)
    {
        if (pointer == (void *) get_val (it))
            return it;
    }

    throw AllocError (AllocErrorType::InternalError, "");
}


void Simple::defrag ()
{
    size_t free_offset = get_val (m_first_free_block);
    while (free_offset != m_first_free_block)
    {
        auto free_info = get_info (free_offset);
        if (free_offset + free_info.second
                == _base_len - get_val (m_descr_table_size) * 2 * size_t_size - 4 * size_t_size)
            break;

        //        size_t point = free_info.first + free_info.second;

        size_t descr = find_descriptor_for_memory (free_offset + free_info.second);
        auto descr_info = get_info (descr);
        size_t descr_offset_debug = (size_t)((char *)descr_info.first - (char *)_base);
        descr_offset_debug += 0;

        memmove ((char *)_base + free_offset, (void *) descr_info.first,
                 descr_info.second);

        put_val (descr, (size_t) ((char *)_base + free_offset));

        size_t block_end =
                (size_t) ((char *) descr_info.first - (size_t) _base) +
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
        free_offset = get_val (m_first_free_block);
    }
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
