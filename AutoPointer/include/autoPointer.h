#pragma once
#include <atomic>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <memory>  // 添加这个头文件

namespace auto_ptr
{
    // 引用计数控制块
    class ControlBlock {
    public:
        std::atomic<int> shared_count;
        std::atomic<int> weak_count;

        ControlBlock() : shared_count(1), weak_count(0) {}
        virtual ~ControlBlock() = default;
        virtual void destroy() = 0;
        virtual void delete_self() = 0;  // 添加自我删除接口
    };

    template<typename T>
    class ControlBlockImpl : public ControlBlock {
    public:
        T* ptr;

        ControlBlockImpl(T* p) : ControlBlock(), ptr(p) {}

        void destroy() override {
            delete ptr;
        }

        void delete_self() override {
            delete this;
        }
    };

    enum class _PubPTR { shared, unique, weak, custom };

    // 主模板实现
    template<typename T, _PubPTR pub, typename Deleter = std::default_delete<T>>
    class autoPointer {
    private:
        T* ptr_;
        ControlBlock* control_block_;
        Deleter deleter_;

    public:
        // 默认构造函数
        autoPointer() : ptr_(nullptr), control_block_(nullptr), deleter_(Deleter{}) {}

        // 从原始指针构造
        explicit autoPointer(T* ptr, Deleter deleter = Deleter{})
            : ptr_(ptr), control_block_(nullptr), deleter_(deleter)
        {
            if constexpr (pub == _PubPTR::shared) {
                if (ptr_) {
                    control_block_ = new ControlBlockImpl<T>(ptr_);
                }
            }
        }

        // 拷贝构造
        autoPointer(const autoPointer& other)
            : ptr_(other.ptr_), control_block_(other.control_block_), deleter_(other.deleter_)
        {
            if constexpr (pub == _PubPTR::shared) {
                if (control_block_) {
                    control_block_->shared_count++;
                }
            }
            else if constexpr (pub == _PubPTR::unique) {
                static_assert(pub != _PubPTR::unique,
                    "unique_ptr cannot be copied");
            }
            else if constexpr (pub == _PubPTR::weak) {
                if (control_block_) {
                    control_block_->weak_count++;
                }
            }
        }

        // 移动构造
        autoPointer(autoPointer&& other) noexcept
            : ptr_(std::exchange(other.ptr_, nullptr)),
            control_block_(std::exchange(other.control_block_, nullptr)),
            deleter_(std::move(other.deleter_))
        {
        }

        // 析构函数
        ~autoPointer() {
            release_impl();
        }

        // 拷贝赋值
        autoPointer& operator=(const autoPointer& other) {
            if (this != &other) {
                release_impl();
                ptr_ = other.ptr_;
                control_block_ = other.control_block_;
                deleter_ = other.deleter_;

                if constexpr (pub == _PubPTR::shared) {
                    if (control_block_) {
                        control_block_->shared_count++;
                    }
                }
                else if constexpr (pub == _PubPTR::weak) {
                    if (control_block_) {
                        control_block_->weak_count++;
                    }
                }
                else if constexpr (pub == _PubPTR::unique) {
                    static_assert(pub != _PubPTR::unique,
                        "unique_ptr cannot be copied");
                }
            }
            return *this;
        }

        // 移动赋值
        autoPointer& operator=(autoPointer&& other) noexcept {
            if (this != &other) {
                release_impl();
                ptr_ = std::exchange(other.ptr_, nullptr);
                control_block_ = std::exchange(other.control_block_, nullptr);
                deleter_ = std::move(other.deleter_);
            }
            return *this;
        }

        // 运算符重载
        T* operator->() const {
            if constexpr (pub == _PubPTR::weak) {
                if (!is_expired()) {
                    return ptr_;
                }
                throw std::runtime_error("weak_ptr expired");
            }
            return ptr_;
        }

        T& operator*() const {
            if constexpr (pub == _PubPTR::weak) {
                if (!is_expired()) {
                    return *ptr_;
                }
                throw std::runtime_error("weak_ptr expired");
            }
            return *ptr_;
        }

        explicit operator bool() const { return ptr_ != nullptr; }

        // 获取原始指针
        T* get() const { return ptr_; }

        // 获取删除器
        Deleter& get_deleter() { return deleter_; }
        const Deleter& get_deleter() const { return deleter_; }

        // 释放所有权
        T* release() {
            if constexpr (pub == _PubPTR::unique) {
                T* temp = ptr_;
                ptr_ = nullptr;
                return temp;
            }
            else {
                static_assert(pub == _PubPTR::unique,
                    "release() only available for unique_ptr");
                return nullptr;
            }
        }

        // 重置指针
        void reset(T* ptr = nullptr) {
            release_impl();
            ptr_ = ptr;
            if constexpr (pub == _PubPTR::shared) {
                if (ptr_) {
                    control_block_ = new ControlBlockImpl<T>(ptr_);
                }
            }
            else if constexpr (pub == _PubPTR::weak)
            {
                static_assert(pub != _PubPTR::weak,
                    "weak_ptr cannot own resource directly");
            }
        }

        // 重置指针并更换删除器
        void reset(T* ptr, Deleter deleter) {
            release_impl();
            ptr_ = ptr;
            deleter_ = deleter;
            if constexpr (pub == _PubPTR::shared) {
                if (ptr_) {
                    control_block_ = new ControlBlockImpl<T>(ptr_);
                }
            }
        }

        // 获取引用计数
        int use_count() const {
            if constexpr (pub == _PubPTR::shared || pub == _PubPTR::weak) {
                return control_block_ ? control_block_->shared_count.load() : 0;
            }
            else {
                return ptr_ ? 1 : 0;
            }
        }

        // 检查是否过期
        bool is_expired() const {
            if constexpr (pub == _PubPTR::weak) {
                return !control_block_ || control_block_->shared_count == 0;
            }
            return false;
        }

        // 从 weak_ptr 升级到 shared_ptr
        autoPointer<T, _PubPTR::shared, Deleter> lock() const {
            static_assert(pub == _PubPTR::weak, "lock() only for weak_ptr");
            if (!is_expired()) {
                return autoPointer<T, _PubPTR::shared, Deleter>(*this);
            }
            return autoPointer<T, _PubPTR::shared, Deleter>();
        }

        // 交换两个指针
        void swap(autoPointer& other) noexcept {
            std::swap(ptr_, other.ptr_);
            std::swap(control_block_, other.control_block_);
            std::swap(deleter_, other.deleter_);
        }

        // 比较运算符
        friend bool operator==(const autoPointer& a, const autoPointer& b) {
            return a.get() == b.get();
        }

        friend bool operator==(const autoPointer& a, std::nullptr_t) {
            return a.get() == nullptr;
        }

        friend bool operator==(std::nullptr_t, const autoPointer& a) {
            return a.get() == nullptr;
        }

        friend bool operator!=(const autoPointer& a, const autoPointer& b) {
            return !(a == b);
        }

        friend bool operator!=(const autoPointer& a, std::nullptr_t) {
            return a.get() != nullptr;
        }

        friend bool operator!=(std::nullptr_t, const autoPointer& a) {
            return a.get() != nullptr;
        }

    private:
        void release_impl() {
            if constexpr (pub == _PubPTR::shared) {
                if (control_block_) {
                    if (--control_block_->shared_count == 0) {
                        control_block_->destroy();
                        if (control_block_->weak_count == 0) {
                            control_block_->delete_self();  // 修复：删除控制块本身
                        }
                    }
                }
            }
            else if constexpr (pub == _PubPTR::weak) {
                if (control_block_) {
                    if (--control_block_->weak_count == 0 &&
                        control_block_->shared_count == 0) {
                        control_block_->delete_self();  // 修复：删除控制块本身
                    }
                }
            }
            else if constexpr (pub == _PubPTR::unique || pub == _PubPTR::custom) {
                if (ptr_) {
                    deleter_(ptr_);
                }
            }

            ptr_ = nullptr;
            control_block_ = nullptr;
        }

        // 友元声明
        template<typename U, _PubPTR V, typename D>
        friend class autoPointer;

        // 从 shared_ptr 创建 weak_ptr 的转换构造函数
        template<typename U = T,
            std::enable_if_t<pub == _PubPTR::weak && std::is_same_v<U, T>, int> = 0>
        explicit autoPointer(const autoPointer<T, _PubPTR::shared, Deleter>&shared)
            : ptr_(shared.ptr_), control_block_(shared.control_block_), deleter_(shared.deleter_)
        {
            if (control_block_) {
                control_block_->weak_count++;
            }
        }

        // 从 weak_ptr 构造 shared_ptr
        template<typename U = T,
            std::enable_if_t<pub == _PubPTR::shared && std::is_same_v<U, T>, int> = 0>
        explicit autoPointer(const autoPointer<T, _PubPTR::weak, Deleter>&weak)
            : ptr_(weak.ptr_), control_block_(weak.control_block_), deleter_(weak.deleter_)
        {
            if (!control_block_ || control_block_->shared_count == 0) {
                throw std::runtime_error("weak_ptr expired");
            }
            control_block_->shared_count++;
        }
    };

    // 类型别名定义
    template<typename T>
    using unique_pointer = autoPointer<T, _PubPTR::unique, std::default_delete<T>>;

    template<typename T>
    using shared_pointer = autoPointer<T, _PubPTR::shared, std::default_delete<T>>;

    template<typename T>
    using weak_pointer = autoPointer<T, _PubPTR::weak>;

    // 工厂函数 - 正确的版本
    template<typename T, typename... Args>
    unique_pointer<T> make_uniqueptr(Args&&... args) {
        return unique_pointer<T>(new T(std::forward<Args>(args)...));
    }

    template<typename T, typename... Args>
    shared_pointer<T> make_sharedptr(Args&&... args) {
        return shared_pointer<T>(new T(std::forward<Args>(args)...));
    }

    template<typename T>
    weak_pointer<T> make_weakptr(const shared_pointer<T>& shared) {
        return weak_pointer<T>(shared);
    }

    // 支持自定义删除器的版本
    template<typename T, typename Deleter, typename... Args>
    unique_pointer<T> make_uniqueptr(Deleter deleter, Args&&... args) {
        return unique_pointer<T>(new T(std::forward<Args>(args)...), deleter);
    }

    template<typename T, typename Deleter, typename... Args>
    shared_pointer<T> make_sharedptr(Deleter deleter, Args&&... args) {
        return shared_pointer<T>(new T(std::forward<Args>(args)...), deleter);
    }
}