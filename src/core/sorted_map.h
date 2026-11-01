#ifndef CTAR_SORTED_MAP_H
#define CTAR_SORTED_MAP_H

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

namespace ctar
{
    /// Ordered key-value container backed by a sorted vector.
    ///
    /// This is storage only. Turning a map into bytes -- field order, widths and byte
    /// order -- belongs to the archive format, so the encoding lives with the format
    /// types in file_format.cpp and not here.
    template <std::totally_ordered K, typename V>
    class SortedMap
    {
    private:
        using Entry = std::pair<K, V>;
        std::vector<Entry> data_;

        static constexpr bool CompareKey(const Entry& entry, const K& key) noexcept { return entry.first < key; }

    public:
        using value_type = Entry;
        using iterator = std::vector<Entry>::iterator;
        using const_iterator = std::vector<Entry>::const_iterator;
        using reverse_iterator = std::vector<Entry>::reverse_iterator;
        using const_reverse_iterator = std::vector<Entry>::const_reverse_iterator;

        iterator begin() noexcept { return data_.begin(); }
        iterator end() noexcept { return data_.end(); }
        const_iterator begin() const noexcept { return data_.begin(); }
        const_iterator end() const noexcept { return data_.end(); }
        const_iterator cbegin() const noexcept { return data_.cbegin(); }
        const_iterator cend() const noexcept { return data_.cend(); }
        reverse_iterator rbegin() noexcept { return data_.rbegin(); }
        reverse_iterator rend() noexcept { return data_.rend(); }
        const_reverse_iterator rbegin() const noexcept { return data_.rbegin(); }
        const_reverse_iterator rend() const noexcept { return data_.rend(); }
        const_reverse_iterator crbegin() const noexcept { return data_.crbegin(); }
        const_reverse_iterator crend() const noexcept { return data_.crend(); }

        [[nodiscard]] const std::vector<Entry>& data() const noexcept { return data_; }
        [[nodiscard]] std::vector<Entry>& data() noexcept { return data_; }

        void reserve(size_t n) noexcept { data_.reserve(n); }
        [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
        [[nodiscard]] size_t size() const noexcept { return data_.size(); }
        void clear() noexcept { data_.clear(); }
        void swap(SortedMap& other) noexcept { data_.swap(other.data_); }

        void put(K key, V value) noexcept
        {
            auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareKey);
            if (it != data_.end() && it->first == key)
            {
                it->second = std::move(value);
            }
            else
            {
                data_.insert(it, Entry{std::move(key), std::move(value)});
            }
        }

        [[nodiscard]] iterator find(const K& key) noexcept
        {
            auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareKey);
            return (it != data_.end() && it->first == key) ? it : data_.end();
        }

        [[nodiscard]] const_iterator find(const K& key) const noexcept
        {
            auto it = std::lower_bound(data_.cbegin(), data_.cend(), key, CompareKey);
            return (it != data_.cend() && it->first == key) ? it : data_.cend();
        }

        template <std::default_initializable U = V>
        U& operator[](const K& key)
        {
            auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareKey);
            if (it != data_.end() && it->first == key)
                return it->second;
            return data_.emplace(it, key, U{})->second;
        }
    };

    // ADL swap
    template <typename K, typename V>
    void swap(SortedMap<K, V>& lhs, SortedMap<K, V>& rhs) noexcept
    {
        lhs.swap(rhs);
    }

} // namespace ctar
#endif // CTAR_SORTED_MAP_H
