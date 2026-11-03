#pragma once

#include <memory>
#include <utility>
#include <vector>

namespace Utilities {

template <typename T>
class SharedBuffer {
public:
    using Container = std::vector<T>;
    using const_iterator = typename Container::const_iterator;
    SharedBuffer() = default;
    SharedBuffer(const SharedBuffer&) = default;
    SharedBuffer(SharedBuffer&&) noexcept = default;
    SharedBuffer& operator=(const SharedBuffer&) = default;
    SharedBuffer& operator=(SharedBuffer&&) noexcept = default;
    SharedBuffer& operator=(Container values) {
        values_ = std::make_shared<Container>(std::move(values));
        immutable_ = false;
        return *this;
    }
    const Container& view() const {
        static const Container empty;
        return values_ ? *values_ : empty;
    }
    operator const Container&() const { return view(); }
    std::shared_ptr<const Container> snapshot() const { return values_; }
    void share(std::shared_ptr<const Container> values) { values_ = std::const_pointer_cast<Container>(std::move(values)); immutable_ = true; }
    bool empty() const { return view().empty(); }
    std::size_t size() const { return view().size(); }
    const T* data() const { return view().data(); }
    const T& front() const { return view().front(); }
    const T& back() const { return view().back(); }
    const T& operator[](std::size_t index) const { return view()[index]; }
    const_iterator begin() const { return view().begin(); }
    const_iterator end() const { return view().end(); }
    void clear() { values_.reset(); }
    void push_back(T value) { writable().push_back(std::move(value)); }
    template <typename Iterator>
    void insert(const_iterator at, Iterator first, Iterator last) {
        const auto offset = at - begin();
        auto& values = writable();
        values.insert(values.begin() + offset, first, last);
    }
private:
    std::shared_ptr<Container> values_;
    bool immutable_ = false;
    Container& writable() {
        if (!values_) values_ = std::make_shared<Container>();
        else if (immutable_ || values_.use_count() > 1) values_ = std::make_shared<Container>(*values_);
        immutable_ = false;
        return *values_;
    }
};

}
