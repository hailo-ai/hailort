/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#pragma once

#include <pybind11/pybind11.h>
#include <string>
#include <exception>

namespace py = pybind11;


class HailoRTException : public std::runtime_error {
    public:
        explicit HailoRTException(const std::string &what) : std::runtime_error(what) {}
};

class HailoRTCustomException : public HailoRTException {
    public:
        explicit HailoRTCustomException(const std::string &what) : HailoRTException(what) {}
};

class HailoRTStatusException : public HailoRTException {
    public:
        explicit HailoRTStatusException(const std::string &what) : HailoRTException(what) {}
};
    

#define EXIT_WITH_ERROR(__message)                                                  \
        throw HailoRTCustomException(std::string(__message));

#define THROW_STATUS_ERROR(__status)                                                \
    do {                                                                            \
        throw HailoRTStatusException(std::to_string((__status)));                   \
    } while (0)

#define VALIDATE_STATUS(__status)                                                   \
    do {                                                                            \
        if (HAILO_SUCCESS != (__status)) {                                          \
            THROW_STATUS_ERROR((__status));                                         \
        }                                                                           \
    } while (0)

#define VALIDATE_NOT_NULL(__ptr, __status)                                          \
    do {                                                                            \
        if (nullptr == (__ptr)) {                                                   \
            throw HailoRTStatusException(std::to_string(__status));                 \
        }                                                                           \
    } while (0)

#define VALIDATE_EXPECTED(__expected)                                               \
    do {                                                                            \
        const auto &expected_object = (__expected);                                 \
        if (!expected_object) {                                                     \
            throw HailoRTStatusException(std::to_string(expected_object.status())); \
        }                                                                           \
    } while (0)

#define UNION_PROPERTY(__union_type, __field_type, __field_name)                    \
    .def_property(#__field_name,                                                    \
        [](__union_type& self) -> const __field_type&                               \
        {                                                                           \
            return self.__field_name;                                               \
        },                                                                          \
        nullptr)

#define STREAM_PARAMETERS_UNION_PROPERTY(__property_name, __property_type, __interface_value, __direction_value) \
    .def_property(#__property_name,                                                                               \
        [](hailo_stream_parameters_t& self) -> const __property_type&                                             \
        {                                                                                                         \
            if (__interface_value != self.stream_interface) {                                                     \
                std::cerr << "Stream params interface is not " << __interface_value << ".";                       \
                THROW_STATUS_ERROR(HAILO_INVALID_OPERATION);                                                      \
            }                                                                                                     \
            if (__direction_value != self.direction) {                                                            \
                std::cerr << "Stream params direction is not " << __direction_value << ".";                       \
                THROW_STATUS_ERROR(HAILO_INVALID_OPERATION);                                                      \
            }                                                                                                     \
            return self.__property_name;                                                                          \
        },                                                                                                        \
        [](hailo_stream_parameters_t& self, const __property_type& value)                                         \
        {                                                                                                         \
            if (__interface_value != self.stream_interface) {                                                     \
                std::cerr << "Stream params interface is not " << __interface_value << ".";                       \
                THROW_STATUS_ERROR(HAILO_INVALID_OPERATION);                                                      \
            }                                                                                                     \
            if (__direction_value != self.direction) {                                                            \
                std::cerr << "Stream params direction is not " << __direction_value << ".";                       \
                THROW_STATUS_ERROR(HAILO_INVALID_OPERATION);                                                      \
            }                                                                                                     \
            self.__property_name = value;                                                                         \
        })

// From https://github.com/pybind/pybind11/issues/1446
// A custom shared ptr that releases the GIL before freeing the resource.
template <typename T>
class nogil_shared_ptr {
private:
    std::shared_ptr<T> ptr;

public:
    template <typename... Args>
    nogil_shared_ptr(Args&&... args)
        : ptr(std::forward<Args>(args)...)
    {
    }

    ~nogil_shared_ptr()
    {
        pybind11::gil_scoped_release nogil;
        ptr.reset();
    }

    T& operator*() const noexcept { return *ptr; }
    T* operator->() const noexcept { return ptr.get(); }
    operator std::shared_ptr<T>() const noexcept { return ptr; }
    T* get() const noexcept { return ptr.get(); }
};

PYBIND11_DECLARE_HOLDER_TYPE(T, nogil_shared_ptr<T>)
