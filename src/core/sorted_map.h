#ifndef CTAR_SORTED_MAP_H
#define CTAR_SORTED_MAP_H

#include <algorithm>
#include <concepts>
#include <utility>
#include <vector>

#include "src/utils/status.h"
#include "src/utils/coding.h"
#include "src/utils/serialize_helper.h"

namespace ctar
{

    constexpr size_t DefaultInnerVectorSize = 64;

    template <typename T>
    concept IsContiguousTrivial = requires(const T& t) {
        { t.data() } -> std::same_as<const typename T::value_type*>;
        { t.size() } -> std::convertible_to<size_t>;
    } && std::is_trivially_copyable_v<typename T::value_type> && !std::same_as<T, std::string>;

    template <typename V>
    concept AllowedValueType = std::is_trivially_copyable_v<V> || IsContiguousTrivial<V>;

    template <typename K>
    concept AllowedKeyType = std::totally_ordered<K> && std::is_trivially_copyable_v<K>;

    template <AllowedKeyType K, AllowedValueType V>
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

        [[nodiscard]] const char* raw_data() const noexcept { return reinterpret_cast<const char*>(data_.data()); }

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

        [[nodiscard]] size_t serialized_size() const noexcept
            requires std::is_trivially_copyable_v<V>
        {
            return sizeof(uint64_t) + data_.size() * sizeof(Entry);
        }

        [[nodiscard]] size_t serialized_size() const noexcept
            requires IsContiguousTrivial<V>
        {
            size_t total = sizeof(uint64_t);
            for (const auto& entry : data_)
            {
                total += sizeof(K) + sizeof(uint64_t) + entry.second.size() * sizeof(typename V::value_type);
            }
            return total;
        }

        void serialize(std::string* dst) const
            requires std::is_trivially_copyable_v<V>
        {
            const size_t cnt = data_.size();
            PutFixed64(dst, cnt);
            if (cnt == 0)
                return;
            const size_t byte_len = cnt * sizeof(Entry);
            dst->append(raw_data(), byte_len);
        }

        void deserialize(const char*& ptr)
            requires std::is_trivially_copyable_v<V>
        {
            const size_t cnt = DecodeFixed64(ptr);
            skip_ptr(ptr, FIX64_LEN);
            data_.clear();
            if (cnt == 0)
                return;

            const auto* src = reinterpret_cast<const Entry*>(ptr);
            data_.assign(src, src + cnt);
            skip_ptr(ptr, cnt * sizeof(Entry));
        }

        void serialize(std::string* dst) const
            requires IsContiguousTrivial<V>
        {
            using T = typename V::value_type;
            static_assert((std::is_integral_v<K> || std::is_enum_v<K>) && sizeof(K) <= sizeof(uint64_t));

            const size_t cnt = data_.size();
            PutFixed64(dst, cnt);

            for (const auto& entry : data_)
            {
                PutFixed64(dst, static_cast<uint64_t>(entry.first));
                PutFixed64(dst, entry.second.size());
                if (!entry.second.empty())
                {
                    const char* vec_raw = reinterpret_cast<const char*>(entry.second.data());
                    dst->append(vec_raw, entry.second.size() * sizeof(T));
                }
            }
        }

        void deserialize(const char*& ptr)
            requires IsContiguousTrivial<V>
        {
            using T = typename V::value_type;
            static_assert((std::is_integral_v<K> || std::is_enum_v<K>) && sizeof(K) <= sizeof(uint64_t));

            const size_t cnt = DecodeFixed64(ptr);
            skip_ptr(ptr, FIX64_LEN);
            data_.clear();

            std::vector<T> temp_buf;
            temp_buf.reserve(kDefaultBufferSize);

            for (size_t i = 0; i < cnt; ++i)
            {
                const K key = static_cast<K>(DecodeFixed64(ptr));
                skip_ptr(ptr, FIX64_LEN);

                const size_t vec_len = DecodeFixed64(ptr);
                skip_ptr(ptr, FIX64_LEN);

                temp_buf.clear();
                if (vec_len > 0)
                {
                    const T* src = reinterpret_cast<const T*>(ptr);
                    temp_buf.assign(src, src + vec_len);
                    skip_ptr(ptr, vec_len * sizeof(T));
                }
                put(key, std::move(temp_buf));
            }
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