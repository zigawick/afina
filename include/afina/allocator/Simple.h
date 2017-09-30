#ifndef AFINA_ALLOCATOR_SIMPLE_H
#define AFINA_ALLOCATOR_SIMPLE_H

#include <string>
#include <cstddef>
#include <vector>

namespace Afina {
namespace Allocator {

  constexpr size_t size_t_size = sizeof (size_t);

// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Pointer;

/**
 * Wraps given memory area and provides defagmentation allocator interface on
 * the top of it.
 *
 * Allocator instance doesn't take ownership of wrapped memmory and do not delete it
 * on destruction. So caller must take care of resource cleaup after allocator stop
 * being needs
 */
// TODO: Implements interface to allow usage as C++ allocators
class Simple {
public:
    Simple(void *base, const size_t size);

    /**
     * TODO: semantics
     * @param N size_t
     */
    Pointer alloc(size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     * @param N size_t
     */
    void realloc(Pointer &p, size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     */
    void free(Pointer &p);

    /**
     * TODO: semantics
     */
    void defrag();

    /**
     * TODO: semantics
     */
    std::vector<size_t> dump() const;
private:
    std::pair<size_t, bool> get_new_descriptor();
    size_t get_val(size_t offset) const;
    std::pair<size_t, size_t> get_info(size_t offset);
    bool is_in_free(size_t offset);
    size_t find_descriptor_for_memory(size_t point);
    void move_by_byte(size_t descr, size_t point);
    void put_val(size_t offset, size_t value);

private:
    void *_base;
    const size_t _base_len;

    size_t m_first_free_block = _base_len - 1 * size_t_size;
    size_t m_descr_table_size = _base_len - 2 * size_t_size;
    size_t m_first_free_descr = _base_len - 3 * size_t_size;
    size_t m_free_space_count = _base_len - 4 * size_t_size;
};

} // namespace Allocator
} // namespace Afina
#endif // AFINA_ALLOCATOR_SIMPLE_H
