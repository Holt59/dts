#pragma once

#include <initializer_list>
#include <type_traits>
#include <typeinfo>

namespace dts {

    template <class T>
    struct in_place_type_t {
        explicit in_place_type_t() = default;
    };

    template <class T>
    constexpr in_place_type_t<T> in_place_type{};

    class bad_any_cast : public std::bad_cast { };

    class any {

        // traits

        template <class T>
        struct is_in_place_type: std::false_type { };
        template <class T>
        struct is_in_place_type<in_place_type_t<T>>: std::true_type { };

        // attributes

        const std::type_info *info{&typeid(void)};
        void *data{nullptr};

        struct {
            void (*copy_construct)(void*, const void*){nullptr};
            void (*destruct)(void*){nullptr};

        } manager;

        // helpers

        template <class ValueType, class... Args>
        ValueType& in_place_construct(Args&&... args) {

            // update type_info
            info = &typeid(ValueType);

            // update manager
            manager.copy_construct = [](void *dst, const void* src) {
                dst = new char[sizeof(ValueType)];
                new(dst) ValueType(*static_cast<const ValueType*>(src));
            };
            manager.destruct = [](void *src) {
                (*static_cast<ValueType*>(src)).~ValueType();
            };

            // allocate storage and construct object
            data = new char[sizeof(ValueType)];
            return *(new(data) ValueType(std::forward<Args>(args)...));
        }

    public:

        // constructors

        constexpr any() noexcept = default;

        any(const any& other) :
            info(other.info),
            manager(other.manager) {
            if (other.data) {
                manager.copy_construct(data, other.data);
            }
        }

        any(any&& other) {
            other.swap(*this);
        }

        template <class ValueType,
                  std::enable_if_t<
                      !std::is_same<std::decay_t<ValueType>, any>::value
                      && !is_in_place_type<std::decay_t<ValueType>>::value> * = nullptr>
        any(ValueType &&value) {
            static_assert(std::is_copy_constructible<std::decay_t<ValueType>>::value,
                          "Attempt to construct any from a non-copyable type.");
            in_place_construct<std::decay_t<ValueType>>(
                std::forward<std::decay_t<ValueType>>(value));
        }

        template <class ValueType, class... Args,
                  std::enable_if_t<
                      std::is_constructible<std::decay_t<ValueType>, Args...>::value
                      && std::is_copy_constructible<std::decay_t<ValueType>>::value> * = nullptr>
        explicit any(in_place_type_t<ValueType>, Args&&... args) {
            in_place_construct<std::decay_t<ValueType>>(std::forward<Args>(args)... );
        }

        template <class ValueType, class U, class... Args,
                  std::enable_if_t<
                      std::is_constructible<std::decay_t<ValueType>,
                                            std::initializer_list<U>, Args...>::value
                      && std::is_copy_constructible<std::decay_t<ValueType>>::value> * = nullptr>
        explicit any(in_place_type_t<ValueType>, std::initializer_list<U> il,
                     Args&&... args ) {
            in_place_construct<std::decay_t<ValueType>>(il, std::forward<Args>(args)... );
        }

        // assignment

        any& operator=(const any& rhs) {
            any(rhs).swap(*this);
            return *this;
        }

        any& operator=(any&& rhs) noexcept {
            any(std::move(rhs)).swap(*this);
            return *this;
        }

        template <class ValueType,
                  std::enable_if_t<
                      !std::is_same<std::decay_t<ValueType>, any>::value
                      && std::is_copy_constructible<std::decay_t<ValueType>>::value> * = nullptr>
        any& operator=(ValueType&& rhs) {
            any(std::forward<ValueType>(rhs)).swap(*this);
            return *this;
        }

        // destructor
        ~any() {
            reset();
        }

        // modifiers

        template <class ValueType, class... Args,
                  std::enable_if_t<
                      std::is_constructible<std::decay_t<ValueType>, Args...>::value
                      && std::is_copy_constructible<std::decay_t<ValueType>>::value> * = nullptr>
        std::decay_t<ValueType>& emplace(Args&&... args) {
            reset();
            return in_place_construct<std::decay_t<ValueType>>(std::forward<Args>(args)... );
        }

        template <class ValueType, class U, class... Args,
                  std::enable_if_t<
                      std::is_constructible<std::decay_t<ValueType>,
                                            std::initializer_list<U>, Args...>::value
                      && std::is_copy_constructible<std::decay_t<ValueType>>::value> * = nullptr>
        std::decay_t<ValueType>& emplace(std::initializer_list<U> il, Args&&... args) {
            reset();
            return in_place_construct<std::decay_t<ValueType>>(il, std::forward<Args>(args)... );
        }


        void reset() noexcept {
            if (has_value()) {
                manager.destruct(data);
                delete[] static_cast<char*>(data);
                data = nullptr;
                info = &typeid(void);
            }
        }

        void swap(any& other) noexcept {
            std::swap(info, other.info);
            std::swap(data, other.data);
            std::swap(manager, other.manager);
        }

        // accessor

        const std::type_info& type() const noexcept { return *info; }
        bool has_value() const noexcept { return data; }

        // any_cast

        template<class ValueType>
        friend const ValueType* any_cast(const any* operand) noexcept {
            return reinterpret_cast<const ValueType*>(operand->data);
        }

    };

    template <class ValueType>
    ValueType* any_cast(any* operand) noexcept {
        return const_cast<ValueType*>(any_cast<ValueType>((const any*)operand));
    }

    template <class ValueType>
    ValueType any_cast(const any& operand) {
        using U = std::remove_cv_t<std::remove_reference_t<ValueType>>;
        if (typeid(U) != operand.type()) { throw bad_any_cast(); }
        return static_cast<ValueType>(*any_cast<U>(&operand));
    }

    template <class ValueType>
    ValueType any_cast(any& operand) {
        using U = std::remove_cv_t<std::remove_reference_t<ValueType>>;
        if (typeid(U) != operand.type()) { throw bad_any_cast(); }
        return static_cast<ValueType>(*any_cast<U>(&operand));
    }

    template <class ValueType>
    ValueType any_cast(any&& operand) {
        using U = std::remove_cv_t<std::remove_reference_t<ValueType>>;
        if (typeid(U) != operand.type()) { throw bad_any_cast(); }
        return static_cast<ValueType>(std::move(*any_cast<U>(&operand)));
    }

}
