/**
 * @file mercury_fields.hpp
 * @brief Contains all memory fields related to mercury
 */

#ifndef _HW_DEBUG_MERCURY_FIELDS_H_
#define _HW_DEBUG_MERCURY_FIELDS_H_

#include "memory_commands.hpp"


class QddcField : public Field {
public:
    QddcField();

    virtual size_t elements_count() const override;
    virtual std::string print_element(MemorySource& memory, size_t index) const override;

private:
    bool is_enabled(MemorySource &memory, size_t index) const;
    uint32_t shmifo_id(MemorySource &memory, size_t index) const;
    std::string mode(MemorySource &memory, size_t index) const;
};

class QsdcField : public Field {
public:
    QsdcField();

    virtual size_t elements_count() const override;
    virtual std::string print_element(MemorySource& memory, size_t index) const override;

private:
    bool is_enabled(MemorySource &memory, size_t index) const;
    uint32_t shmifo_id(MemorySource &memory, size_t index) const;
};


class QdmcField : public Field {
public:
    QdmcField();

    virtual size_t elements_count() const override;
    virtual std::string print_element(MemorySource& memory, size_t index) const override;

private:
    bool is_enabled(MemorySource &memory, size_t index) const;
    uint64_t base_address(MemorySource &memory, size_t index) const;
    uint32_t descriptors_count(MemorySource &memory, size_t index) const;
    uint32_t descriptors_per_irq(MemorySource &memory, size_t index) const;
};

class QsmcField : public Field {
public:
    QsmcField();

    virtual size_t elements_count() const override;
    virtual std::string print_element(MemorySource& memory, size_t index) const override;

private:
    bool is_enabled(MemorySource &memory, size_t index) const;
    uint64_t base_address(MemorySource &memory, size_t index) const;
    uint32_t descriptors_count(MemorySource &memory, size_t index) const;
    std::string mode(MemorySource &memory, size_t index) const;
};

#endif /* _HW_DEBUG_MERCURY_FIELDS_H_ */
