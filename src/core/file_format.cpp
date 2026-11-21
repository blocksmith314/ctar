#include <algorithm>
#include <cerrno>
#include <cstring>
#include <grp.h>
#include <limits>
#include <pwd.h>
#include <ranges>
#include <sys/stat.h>
#include <system_error>
#include <type_traits>

#include "file_format.h"
#include "src/utils/coding.h"
#include "src/utils/logger.h"
#include "src/utils/serialize_helper.h"
#include "src/utils/tools.h"
namespace ctar
{

namespace
{
    // --- bounds-checked decoding -------------------------------------------------
    // The archive is untrusted: every size and count read from it is validated
    // against the bytes that are actually present before anything is allocated.

    constexpr size_t kHandleSize = 2 * sizeof(uint64_t);
    constexpr size_t kEntryMetaSize = 176;
    constexpr size_t kEntryRecordSize = kHandleSize + kEntryMetaSize;        // 192
    constexpr size_t kMapEntryMinSize = sizeof(uint64_t) + kEntryRecordSize; // 200
    constexpr size_t kIdVectorMapEntryMinSize = 2 * sizeof(uint64_t);        // 16

    [[nodiscard]] bool HasBytes(const char* ptr, const char* end, size_t need) noexcept
    {
        return ptr != nullptr && end != nullptr && ptr <= end && static_cast<size_t>(end - ptr) >= need;
    }

    [[nodiscard]] Status TruncatedMetadata()
    {
        return error_corruption("corrupt archive: metadata is truncated");
    }

    [[nodiscard]] Status CorruptBlockMeta(std::string_view detail)
    {
        return error_corruption("corrupt archive: {}", detail);
    }

    [[nodiscard]] Status ReadFixed64(const char*& ptr, const char* end, uint64_t& value)
    {
        if (!HasBytes(ptr, end, sizeof(uint64_t)))
        {
            return TruncatedMetadata();
        }
        value = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        return {};
    }

}

    void EntryMeta::FormatOutput(std::ostream& os, const DirFileRef& dir_file_ref, const OutputMode mode,
                                 const bool human_readable, const ListColWidths& col_widths) const
    {
        const auto it = std::ostreambuf_iterator<char>(os);

        std::string_view owner(owner_name, strnlen(owner_name, sizeof(owner_name)));
        std::string_view group(group_name, strnlen(group_name, sizeof(group_name)));
        constexpr long long ms_div = 1000;
        std::string time_str = FormatTime(modify_time / ms_div);
        std::string format_file_size;
        if (human_readable)
        {
            format_file_size = HumanReadableBytes(file_orig_size);
        }
        else
        {
            format_file_size = std::to_string(file_orig_size);
        }

        if (mode == OutputMode::LIST)
        {
            std::string perm;
            perm.reserve(10);
            perm += entry_type == EntryType::DIRECTORY ? 'd' : '-';
            perm += PermBitsToString(permissions);
            std::format_to(it, list::kListRowFormat, perm, owner,
                           col_widths.owner_name_width, group, col_widths.group_name_width, format_file_size,
                           col_widths.file_size_width, time_str, dir_file_ref.dir_name, dir_file_ref.file_name);
        }
        else
        {
            std::format_to(it, tsv::kTsvRowFormat, block_id, dir_file_ref.dir_id, dir_file_ref.file_id, permissions, owner,
                           group, modify_time, offset, file_orig_size, file_compressed_size, dir_file_ref.dir_name,
                           dir_file_ref.file_name);
        }
    }


    void PackFileHeader::Serialize(std::string* dst) const
    {
        dst->clear();
        PutFixed32(dst, magic_number);
        PutFixed32(dst, file_format_version.Encode());
        PutFixed32(dst, ctar_version);
        PutFixed32(dst, compressed_param);
        PutFixed64(dst, pack_begin_time);
        PutFixed64(dst, pack_end_time);
        PutFixed64(dst, pack_file_size);
        PutFixed64(dst, file_count);
        PutFixed64(dst, directory_count);
        PutFixed64(dst, block_count);
        PutFixed64(dst, file_data_offset);
        PutFixed64(dst, file_data_original_size);
        PutFixed64(dst, file_data_compressed_size);
        PutFixed64(dst, file_data_compressed_type);
        PutFixed64(dst, metadata_offset);
        PutFixed64(dst, metadata_original_size);
        PutFixed64(dst, metadata_compressed_size);
        PutFixed64(dst, metadata_compressed_type);
        PutFixed64(dst, metadata_checksum);
        dst->resize(kSerializedSize, '\0');
    }

    Status PackFileHeader::Deserialize(const char* ptr, const char* end)
    {
        if (!HasBytes(ptr, end, kUsedSize))
        {
            return TruncatedMetadata();
        }
        magic_number = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        if (magic_number != kMagicNumber)
        {
            return error_corruption("Invalid magic number");
        }
        file_format_version = Version::Decode(DecodeFixed32(ptr));
        ptr += sizeof(uint32_t);
        ctar_version = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        compressed_param = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        auto read64 = [&ptr]() -> uint64_t
        {
            const uint64_t value = DecodeFixed64(ptr);
            ptr += sizeof(uint64_t);
            return value;
        };
        pack_begin_time = read64();
        pack_end_time = read64();
        pack_file_size = read64();
        file_count = read64();
        directory_count = read64();
        block_count = read64();
        file_data_offset = read64();
        file_data_original_size = read64();
        file_data_compressed_size = read64();
        file_data_compressed_type = read64();
        metadata_offset = read64();
        metadata_original_size = read64();
        metadata_compressed_size = read64();
        metadata_compressed_type = read64();
        metadata_checksum = read64();
        return {};
    }

    Status LegacyPackFileHeader::Deserialize(const char*& ptr, const char* end)
    {
        if (!HasBytes(ptr, end, kSerializedSize))
        {
            return TruncatedMetadata();
        }
        magic_number = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        if (magic_number != kMagicNumber)
        {
            return error_corruption("Invalid magic number");
        }
        file_format_version = Version::Decode(DecodeFixed32(ptr));
        ptr += sizeof(uint32_t);
        file_meta_size = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        return {};
    }

    size_t DataBlockMeta::SerializedSize() const
    {
        return FixedFieldTotalSize(block_id, dir_id, block_file_offset, payload_size, block_size, hash_value) +
            ContainerSerializedTotalSize(file_ids, file_orig_sizes, file_compressed_sizes, file_compressed_types,
                                         file_offsets, file_hashes);
    }

    void DataBlockMeta::Serialize(std::string* dst) const
    {
        PutFixed64(dst, block_id);
        PutFixed64(dst, dir_id);
        PutFixed64(dst, block_file_offset);
        PutFixed64(dst, payload_size);
        PutFixed64(dst, block_size);
        PutFixed64(dst, hash_value);

        /* file_ids */
        PutFixed64(dst, file_ids.size());
        if (!file_ids.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_ids.data());
            dst->append(raw, file_ids.size() * sizeof(entry_id_t));
        }

        /* file_orig_sizes */
        PutFixed64(dst, file_orig_sizes.size());
        if (!file_orig_sizes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_orig_sizes.data());
            dst->append(raw, file_orig_sizes.size() * sizeof(uint64_t));
        }

        /* file_compressed_sizes */
        PutFixed64(dst, file_compressed_sizes.size());
        if (!file_compressed_sizes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_compressed_sizes.data());
            dst->append(raw, file_compressed_sizes.size() * sizeof(uint64_t));
        }

        /* file_compressed_types */
        PutFixed64(dst, file_compressed_types.size());
        for (CompressionType type : file_compressed_types)
        {
            PutFixed8(dst, static_cast<uint8_t>(type));
        }

        /* file_offsets */
        PutFixed64(dst, file_offsets.size());
        if (!file_offsets.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_offsets.data());
            dst->append(raw, file_offsets.size() * sizeof(uint64_t));
        }

        /* file_hashes */
        PutFixed64(dst, file_hashes.size());
        if (!file_hashes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_hashes.data());
            dst->append(raw, file_hashes.size() * sizeof(uint64_t));
        }
    }

    Status DataBlockMeta::Deserialize(const char*& ptr, const char* end)
    {
        if (!HasBytes(ptr, end, 6 * sizeof(uint64_t)))
        {
            return TruncatedMetadata();
        }
        block_id = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        dir_id = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        block_file_offset = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        payload_size = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        block_size = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        hash_value = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);

        uint64_t file_count = 0;
        if (auto s = ReadFixed64(ptr, end, file_count); !s)
        {
            return s;
        }
        if (file_count > static_cast<uint64_t>(end - ptr) / sizeof(entry_id_t))
        {
            return TruncatedMetadata();
        }
        file_ids.resize(static_cast<size_t>(file_count));
        if (!file_ids.empty())
        {
            std::memcpy(file_ids.data(), ptr, file_ids.size() * sizeof(entry_id_t));
            ptr += file_ids.size() * sizeof(entry_id_t);
        }

        // All per-file vectors must describe exactly the same files; otherwise unpacking
        // would index past the end of one of them.
        const auto read_u64_vector = [&](std::vector<uint64_t>& values) -> Status
        {
            uint64_t count = 0;
            if (auto s = ReadFixed64(ptr, end, count); !s)
            {
                return s;
            }
            if (count != file_count)
            {
                return CorruptBlockMeta(
                    std::format("block {}: per-file vector count {} does not match file count {}", block_id, count,
                                file_count));
            }
            if (count > static_cast<uint64_t>(end - ptr) / sizeof(uint64_t))
            {
                return TruncatedMetadata();
            }
            values.resize(static_cast<size_t>(count));
            if (!values.empty())
            {
                std::memcpy(values.data(), ptr, values.size() * sizeof(uint64_t));
                ptr += values.size() * sizeof(uint64_t);
            }
            return {};
        };

        if (auto s = read_u64_vector(file_orig_sizes); !s)
        {
            return s;
        }
        if (auto s = read_u64_vector(file_compressed_sizes); !s)
        {
            return s;
        }

        /* file_compressed_types: one byte per file */
        uint64_t type_count = 0;
        if (auto s = ReadFixed64(ptr, end, type_count); !s)
        {
            return s;
        }
        if (type_count != file_count || type_count > static_cast<uint64_t>(end - ptr))
        {
            return CorruptBlockMeta(std::format("block {}: compression type count {} is invalid for file count {}",
                                                block_id, type_count, file_count));
        }
        file_compressed_types.resize(static_cast<size_t>(type_count));
        for (CompressionType& type : file_compressed_types)
        {
            type = static_cast<CompressionType>(DecodeFixed8(ptr));
            ptr += sizeof(uint8_t);
        }

        if (auto s = read_u64_vector(file_offsets); !s)
        {
            return s;
        }
        if (auto s = read_u64_vector(file_hashes); !s)
        {
            return s;
        }

        // Cross-field invariants (payload bounds, kNone sizes, compression types) are
        // checked by ValidateDataBlockMeta() once the caller can name the failing entry.
        return {};
    }

    std::string DataBlockMeta::DebugString() const
    {
        return std::format("data_block_meta:  block_id: {}, block_size: {}, data_beg_pos:{}, dir_id:{}, "
                           "file_id_sizes:{},file_xxhash.size(), serialize_size:{}",
                           block_id, block_size, block_file_offset, dir_id, file_ids.size(), file_hashes.size(),
                           SerializedSize());
    }

    Status ValidateDataBlockMeta(const DataBlockMeta& block_meta, const EntryPathResolver& path_of)
    {
        const size_t file_count = block_meta.file_ids.size();
        // Every per-file vector is indexed below, so guard the shape first: this
        // function is public and must not read past a short vector.
        if (block_meta.file_orig_sizes.size() != file_count ||
            block_meta.file_compressed_sizes.size() != file_count ||
            block_meta.file_compressed_types.size() != file_count || block_meta.file_offsets.size() != file_count ||
            block_meta.file_hashes.size() != file_count)
        {
            return error_corruption("block {}: per-file vectors have different lengths", block_meta.block_id);
        }

        // The stored path is resolved only when there is an error to report, so valid
        // blocks pay no lookup cost. When it is unavailable, fall back to the entry's
        // position in the block instead of an opaque file id.
        const auto entry_label = [&](size_t index) -> std::string
        {
            if (path_of)
            {
                const std::string path = path_of(block_meta.dir_id, block_meta.file_ids[index]);
                if (!path.empty())
                {
                    return std::format("file '{}'", path);
                }
            }
            return std::format("block {} entry {}", block_meta.block_id, index);
        };

        if (block_meta.payload_size > block_meta.block_size)
        {
            return error_corruption("block {}: payload size {} exceeds block size {}", block_meta.block_id,
                                    block_meta.payload_size, block_meta.block_size);
        }
        // LZ4's block format cannot expand a stream beyond roughly 255x: every
        // match-length extension byte encodes at most 255 output bytes. Reject sizes
        // that no valid stream could produce, and sizes the int-based LZ4 API cannot
        // represent. This bounds the memory a hostile archive can make unpack allocate.
        constexpr uint64_t kLz4MaxExpansion = 255;
        const uint64_t int_max = static_cast<uint64_t>(std::numeric_limits<int>::max());
        uint64_t previous_end = 0;
        for (size_t i = 0; i < file_count; ++i)
        {
            // Pack writes the files of a block in scan order, so ids and ranges are both
            // strictly ordered. Rejecting anything else also rules out duplicate ids,
            // which would otherwise make two unpack tasks write the same path.
            if (i > 0 && block_meta.file_ids[i] <= block_meta.file_ids[i - 1])
            {
                return error_corruption("{}: file ids are not strictly increasing ({} after {})", entry_label(i),
                                        block_meta.file_ids[i], block_meta.file_ids[i - 1]);
            }
            if (block_meta.file_offsets[i] > block_meta.payload_size ||
                block_meta.file_compressed_sizes[i] > block_meta.payload_size - block_meta.file_offsets[i])
            {
                return error_corruption("{}: offset {} + stored size {} exceeds payload size {}", entry_label(i),
                                        block_meta.file_offsets[i], block_meta.file_compressed_sizes[i],
                                        block_meta.payload_size);
            }
            if (i > 0 && block_meta.file_offsets[i] < previous_end)
            {
                return error_corruption("{}: offset {} overlaps the previous entry ending at {}", entry_label(i),
                                        block_meta.file_offsets[i], previous_end);
            }
            previous_end = block_meta.file_offsets[i] + block_meta.file_compressed_sizes[i];
            switch (block_meta.file_compressed_types[i])
            {
            case CompressionType::kNone:
                // Raw data is copied verbatim, so the stored size is the original size.
                if (block_meta.file_compressed_sizes[i] != block_meta.file_orig_sizes[i])
                {
                    return error_corruption("{}: raw entry stored size {} != original size {}", entry_label(i),
                                            block_meta.file_compressed_sizes[i], block_meta.file_orig_sizes[i]);
                }
                break;
            case CompressionType::kLZ4:
            case CompressionType::kLZ4HC:
                {
                    const uint64_t original = block_meta.file_orig_sizes[i];
                    const uint64_t compressed = block_meta.file_compressed_sizes[i];
                    if (original > 0 && compressed == 0)
                    {
                        return error_corruption("{}: compressed entry stores 0 bytes for original size {}",
                                                entry_label(i), original);
                    }
                    if (original > int_max || compressed > int_max)
                    {
                        return error_corruption("{}: LZ4 entry original size {} or stored size {} exceeds {}",
                                                entry_label(i), original, compressed, int_max);
                    }
                    if (original > compressed * kLz4MaxExpansion)
                    {
                        return error_corruption(
                            "{}: original size {} exceeds the LZ4 expansion bound of {} x stored size {}",
                            entry_label(i), original, kLz4MaxExpansion, compressed);
                    }
                }
                break;
            default:
                return error_corruption("{}: unknown compression type {}", entry_label(i),
                                        static_cast<uint32_t>(block_meta.file_compressed_types[i]));
            }
        }
        return {};
    }

    PackFileMeta::PackFileMeta()
    {
        dir_name_data.reserve(kEntryNameBufferSize);
        file_name_data.reserve(kEntryNameBufferSize);
        dir_id_to_child_dir_ids.reserve(64);
        dir_id_to_child_file_ids.reserve(64);
        dir_id_to_child_file_sizes.reserve(64);
    }

    void PackFileMeta::Clear()
    {
        dir_name_data.clear();
        file_name_data.clear();
        dir_entry_records.clear();
        file_entry_records.clear();
        dir_id_to_child_dir_ids.clear();
        dir_id_to_child_file_ids.clear();
        dir_id_to_child_file_sizes.clear();
    }

    size_t EntryMeta::SerializedSize() const noexcept
    {
        return FixedFieldTotalSize(entry_type, permissions, block_id, owner_name, group_name, modify_time, offset,
                                   file_orig_size, file_compressed_size);
    }

    void EntryMeta::Serialize(std::string* dst) const
    {
        PutFixed32(dst, static_cast<uint32_t>(entry_type));
        PutFixed32(dst, permissions);
        PutFixed64(dst, block_id);
        dst->append(owner_name, sizeof(owner_name));
        dst->append(group_name, sizeof(group_name));
        PutFixed64(dst, static_cast<uint64_t>(modify_time));
        PutFixed64(dst, offset);
        PutFixed64(dst, file_orig_size);
        PutFixed64(dst, file_compressed_size);
    }

    Status EntryMeta::Deserialize(const char*& ptr, const char* end)
    {
        if (!HasBytes(ptr, end, kEntryMetaSize))
        {
            return TruncatedMetadata();
        }
        entry_type = static_cast<EntryType>(DecodeFixed32(ptr));
        ptr += sizeof(uint32_t);
        permissions = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        block_id = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        std::memcpy(owner_name, ptr, sizeof(owner_name));
        ptr += sizeof(owner_name);
        std::memcpy(group_name, ptr, sizeof(group_name));
        ptr += sizeof(group_name);
        modify_time = static_cast<long long>(DecodeFixed64(ptr));
        ptr += sizeof(uint64_t);
        offset = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        file_orig_size = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        file_compressed_size = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);

        if (entry_type != EntryType::REGULAR && entry_type != EntryType::DIRECTORY)
        {
            return error_corruption("corrupt archive: unknown entry type {}", static_cast<uint32_t>(entry_type));
        }
        return {};
    }

    size_t EntryRecord::SerializedSize() const noexcept
    {
        return FixedFieldTotalSize(name_handle.offset, name_handle.len) + entry_meta.SerializedSize();
    }

    void EntryRecord::Serialize(std::string* dst) const
    {
        PutFixed64(dst, name_handle.offset);
        PutFixed64(dst, name_handle.len);
        entry_meta.Serialize(dst);
    }

    Status EntryRecord::Deserialize(const char*& ptr, const char* end)
    {
        if (!HasBytes(ptr, end, kHandleSize))
        {
            return TruncatedMetadata();
        }
        name_handle.offset = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        name_handle.len = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        return entry_meta.Deserialize(ptr, end);
    }

    namespace
    {

        // SortedMap is a plain container, so the wire format of the maps used by the
        // archive lives here: count, then one key + value per entry, each field with a
        // fixed width and little-endian byte order.

        void SerializeEntryRecordMap(const SortedMap<entry_id_t, EntryRecord>& map, std::string* dst)
        {
            PutFixed64(dst, map.size());
            for (const auto& [key, record] : map)
            {
                PutFixed64(dst, key);
                record.Serialize(dst);
            }
        }

        Status DeserializeEntryRecordMap(const char*& ptr, const char* end, SortedMap<entry_id_t, EntryRecord>& map)
        {
            uint64_t count = 0;
            if (auto s = ReadFixed64(ptr, end, count); !s)
            {
                return s;
            }
            // Reject an impossible count before allocating anything for it.
            if (count > static_cast<uint64_t>(end - ptr) / kMapEntryMinSize)
            {
                return TruncatedMetadata();
            }
            map.clear();
            map.reserve(static_cast<size_t>(count));
            for (uint64_t i = 0; i < count; ++i)
            {
                entry_id_t key = 0;
                if (auto s = ReadFixed64(ptr, end, key); !s)
                {
                    return s;
                }
                EntryRecord record{};
                if (auto s = record.Deserialize(ptr, end); !s)
                {
                    return s;
                }
                map.put(key, std::move(record));
            }
            return {};
        }

        size_t EntryRecordMapSerializedSize(const SortedMap<entry_id_t, EntryRecord>& map)
        {
            size_t total = sizeof(uint64_t);
            for (const auto& [key, record] : map)
            {
                total += sizeof(uint64_t) + record.SerializedSize();
            }
            return total;
        }

        template <typename T>
        void SerializeIdVectorMap(const SortedMap<entry_id_t, std::vector<T>>& map, std::string* dst)
        {
            static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(uint64_t));
            PutFixed64(dst, map.size());
            for (const auto& [key, values] : map)
            {
                PutFixed64(dst, key);
                PutFixed64(dst, values.size());
                for (const T value : values)
                {
                    PutFixed64(dst, static_cast<uint64_t>(value));
                }
            }
        }

        template <typename T>
        Status DeserializeIdVectorMap(const char*& ptr, const char* end, SortedMap<entry_id_t, std::vector<T>>& map)
        {
            static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(uint64_t));
            uint64_t count = 0;
            if (auto s = ReadFixed64(ptr, end, count); !s)
            {
                return s;
            }
            if (count > static_cast<uint64_t>(end - ptr) / kIdVectorMapEntryMinSize)
            {
                return TruncatedMetadata();
            }
            map.clear();
            map.reserve(static_cast<size_t>(count));

            std::vector<T> values;
            for (uint64_t i = 0; i < count; ++i)
            {
                entry_id_t key = 0;
                uint64_t value_count = 0;
                if (auto s = ReadFixed64(ptr, end, key); !s)
                {
                    return s;
                }
                if (auto s = ReadFixed64(ptr, end, value_count); !s)
                {
                    return s;
                }
                if (value_count > static_cast<uint64_t>(end - ptr) / sizeof(uint64_t))
                {
                    return TruncatedMetadata();
                }
                values.clear();
                values.reserve(static_cast<size_t>(value_count));
                for (uint64_t j = 0; j < value_count; ++j)
                {
                    uint64_t value = 0;
                    if (auto s = ReadFixed64(ptr, end, value); !s)
                    {
                        return s;
                    }
                    values.push_back(static_cast<T>(value));
                }
                map.put(key, std::move(values));
            }
            return {};
        }

        template <typename T>
        size_t IdVectorMapSerializedSize(const SortedMap<entry_id_t, std::vector<T>>& map)
        {
            size_t total = sizeof(uint64_t);
            for (const auto& [key, values] : map)
            {
                total += 2 * sizeof(uint64_t) + values.size() * sizeof(uint64_t);
            }
            return total;
        }
    } // namespace

    void PackFileMeta::Serialize(std::string* dst) const
    {
        PutFixed64(dst, file_count);
        PutFixed64(dst, file_data_original_size);
        PutFixed64(dst, file_data_compressed_size);
        PutFixed64(dst, block_padding_size);

        PutFixed64(dst, dir_name_data.size());
        dst->append(dir_name_data);
        PutFixed64(dst, file_name_data.size());
        dst->append(file_name_data);

        SerializeEntryRecordMap(dir_entry_records, dst);
        SerializeEntryRecordMap(file_entry_records, dst);

        SerializeIdVectorMap(dir_id_to_child_dir_ids, dst);
        SerializeIdVectorMap(dir_id_to_child_file_ids, dst);
        SerializeIdVectorMap(dir_id_to_child_file_sizes, dst);
    }

    Status PackFileMeta::Deserialize(const char*& ptr, const char* end)
    {
        Clear();

        if (auto s = ReadFixed64(ptr, end, file_count); !s)
        {
            return s;
        }
        if (auto s = ReadFixed64(ptr, end, file_data_original_size); !s)
        {
            return s;
        }
        if (auto s = ReadFixed64(ptr, end, file_data_compressed_size); !s)
        {
            return s;
        }
        if (auto s = ReadFixed64(ptr, end, block_padding_size); !s)
        {
            return s;
        }

        uint64_t dir_name_size = 0;
        if (auto s = ReadFixed64(ptr, end, dir_name_size); !s)
        {
            return s;
        }
        if (dir_name_size > static_cast<uint64_t>(end - ptr))
        {
            return TruncatedMetadata();
        }
        dir_name_data.assign(ptr, static_cast<size_t>(dir_name_size));
        ptr += dir_name_size;

        uint64_t file_name_size = 0;
        if (auto s = ReadFixed64(ptr, end, file_name_size); !s)
        {
            return s;
        }
        if (file_name_size > static_cast<uint64_t>(end - ptr))
        {
            return TruncatedMetadata();
        }
        file_name_data.assign(ptr, static_cast<size_t>(file_name_size));
        ptr += file_name_size;

        if (auto s = DeserializeEntryRecordMap(ptr, end, dir_entry_records); !s)
        {
            return s;
        }
        if (auto s = DeserializeEntryRecordMap(ptr, end, file_entry_records); !s)
        {
            return s;
        }
        if (auto s = DeserializeIdVectorMap(ptr, end, dir_id_to_child_dir_ids); !s)
        {
            return s;
        }
        if (auto s = DeserializeIdVectorMap(ptr, end, dir_id_to_child_file_ids); !s)
        {
            return s;
        }
        if (auto s = DeserializeIdVectorMap(ptr, end, dir_id_to_child_file_sizes); !s)
        {
            return s;
        }

        // Name handles are offsets into the two string buffers; a corrupt handle would
        // make every later lookup read outside them.
        const auto validate_handles = [](const SortedMap<entry_id_t, EntryRecord>& records,
                                         const std::string& buffer) -> Status
        {
            for (const auto& [key, record] : records)
            {
                const auto& handle = record.name_handle;
                if (handle.offset > buffer.size() || handle.len > buffer.size() - handle.offset)
                {
                    return error_corruption("corrupt archive: entry {} points outside the name buffer", key);
                }
            }
            return {};
        };
        if (auto s = validate_handles(dir_entry_records, dir_name_data); !s)
        {
            return s;
        }
        if (auto s = validate_handles(file_entry_records, file_name_data); !s)
        {
            return s;
        }
        return {};
    }

    size_t PackFileMeta::SerializedSize() const
    {
        size_t total_size =
            FixedFieldTotalSize(file_count, file_data_original_size, file_data_compressed_size, block_padding_size);
        total_size += ContainerSerializedTotalSize(dir_name_data, file_name_data);
        total_size += EntryRecordMapSerializedSize(dir_entry_records);
        total_size += EntryRecordMapSerializedSize(file_entry_records);
        total_size += IdVectorMapSerializedSize(dir_id_to_child_dir_ids);
        total_size += IdVectorMapSerializedSize(dir_id_to_child_file_ids);
        total_size += IdVectorMapSerializedSize(dir_id_to_child_file_sizes);
        return total_size;
    }

    void PackFileHandle::Clear()
    {
        dir_path_to_dir_id_.clear();
        uid_to_name_.clear();
        gid_to_name_.clear();
        dir_id_ = kRootDirId;
        file_id_ = kFileBeginId;
        pack_file_meta_.Clear();
    }

    const std::string& PackFileHandle::GetUserName(const uint32_t uid)
    {
        if (const auto it = uid_to_name_.find(uid); it != uid_to_name_.end())
        {
            return it->second;
        }
        std::string name = "unknown";
        if (passwd* pw = getpwuid(uid); pw != nullptr)
        {
            name = pw->pw_name;
        }
        else
        {
            std::println(std::cerr, "failed to get user name for uid {}, will use unknown instead", uid);
        }
        return uid_to_name_.emplace(uid, std::move(name)).first->second;
    }

    const std::string& PackFileHandle::GetGroupName(const uint32_t gid)
    {
        if (const auto it = gid_to_name_.find(gid); it != gid_to_name_.end())
        {
            return it->second;
        }
        std::string name = "unknown";
        if (const group* gr = getgrgid(gid); gr != nullptr)
        {
            name = gr->gr_name;
        }
        else
        {
            std::println(std::cerr, "failed to get group name for gid {}, will use unknown instead", gid);
        }
        return gid_to_name_.emplace(gid, std::move(name)).first->second;
    }

    void PackFileHandle::AddEntry(const entry_id_t key, std::string_view name, std::string* data_buf,
                                  SortedMap<entry_id_t, EntryRecord>& map, const EntryMeta& file_meta)
    {
        const auto off = static_cast<uint64_t>(data_buf->size());
        const auto len = static_cast<uint64_t>(name.size());
        const Handle handle{off, len};
        data_buf->append(name.data(), name.size());
        map.put(key, {handle, file_meta});
    }

    void PackFileHandle::AddDir(const entry_id_t dir_id, std::string_view dir_path, entry_id_t parent_dir_id,
                                const EntryMeta& file_meta)
    {
        AddEntry(dir_id, dir_path, &pack_file_meta_.dir_name_data, pack_file_meta_.dir_entry_records, file_meta);
        dir_path_to_dir_id_[std::string(dir_path)] = dir_id;
        if (parent_dir_id != 0)
        {
            pack_file_meta_.dir_id_to_child_dir_ids[parent_dir_id].push_back(dir_id);
        }
    }

    ResultStatus<std::string_view> PackFileHandle::GetDirPathByDirId(const entry_id_t dir_id) const
    {
        auto it = pack_file_meta_.dir_entry_records.find(dir_id);
        if (it != pack_file_meta_.dir_entry_records.end())
        {
            const auto& h = it->second.name_handle;
            return std::string_view(pack_file_meta_.dir_name_data.data() + h.offset, h.len);
        }
        return error_io("fail to get dir name of {}", dir_id);
    }

    [[nodiscard]] ResultStatus<entry_id_t> PackFileHandle::GetDirIdByDirPath(const std::string& dir_path) const
    {
        if (const auto it = dir_path_to_dir_id_.find(dir_path); it != dir_path_to_dir_id_.end())
        {
            return it->second;
        }
        return error_not_found("fail to get the dir id of {}", dir_path);
    }

    void PackFileHandle::AddFile(const entry_id_t key, std::string_view file_name, entry_id_t parent_dir_id,
                                 const EntryMeta& file_meta)
    {
        AddEntry(key, file_name, &pack_file_meta_.file_name_data, pack_file_meta_.file_entry_records, file_meta);
        if (parent_dir_id != 0)
        {
            pack_file_meta_.dir_id_to_child_file_ids[parent_dir_id].push_back(key);
            pack_file_meta_.dir_id_to_child_file_sizes[parent_dir_id].push_back(file_meta.file_orig_size);
        }
    }

    ResultStatus<std::string_view> PackFileHandle::GetFileNameByFileId(const entry_id_t key) const
    {
        if (auto it = pack_file_meta_.file_entry_records.find(key); it != pack_file_meta_.file_entry_records.end())
        {
            const auto& h = it->second.name_handle;
            return std::string_view(pack_file_meta_.file_name_data.data() + h.offset, h.len);
        }
        else
        {
            return error_not_found("fail to find the file name of {}", key);
        }
    }

    ResultStatus<ListColWidths> PackFileHandle::GetListWidthByFileId(const entry_id_t key) const
    {
        if (const auto it = pack_file_meta_.file_entry_records.find(key);
            it != pack_file_meta_.file_entry_records.end())
        {
            const auto& meta = it->second.entry_meta;
            return ListColWidths{SafeStrLen(meta.owner_name, sizeof(meta.owner_name)),
                                 SafeStrLen(meta.group_name, sizeof(meta.group_name)),
                                 std::formatted_size("{}", meta.file_orig_size)};
        }
        else
        {
            return error_not_found("fail to find entry by file_id {}", key);
        }
    }


    const std::vector<entry_id_t>& PackFileHandle::GetChildDirIdsByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_dir_ids.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_dir_ids.end()) ? it->second : kEmptyVec;
    }

    const std::vector<entry_id_t>& PackFileHandle::GetChildFileIdsByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_file_ids.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_file_ids.end()) ? it->second : kEmptyVec;
    }

    const std::vector<uint64_t>& PackFileHandle::GetFileSizeByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_file_sizes.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_file_sizes.end()) ? it->second : kEmptyVec;
    }

    void PackFileHandle::SetFileEntryMeta(const entry_id_t file_id, uint64_t block_id, const uint64_t offset,
                                          const uint64_t compressed_size)
    {
        pack_file_meta_.file_entry_records[file_id].entry_meta.block_id = block_id;
        pack_file_meta_.file_entry_records[file_id].entry_meta.offset = offset;
        pack_file_meta_.file_entry_records[file_id].entry_meta.file_compressed_size = compressed_size;
    }

    void PackFileHandle::SerializePackFileMeta(std::string* dst) const { pack_file_meta_.Serialize(dst); }

    Status PackFileHandle::DeserializePackFileMeta(const char*& ptr, const char* end)
    {
        dir_path_to_dir_id_.clear();
        if (auto status = pack_file_meta_.Deserialize(ptr, end); !status)
        {
            return status;
        }
        for (const auto& key : pack_file_meta_.dir_entry_records | std::views::keys)
        {
            if (auto dir_name_opt = GetDirPathByDirId(key); dir_name_opt)
            {
                dir_path_to_dir_id_[std::string(dir_name_opt.value())] = key;
            }
            else
            {
                return error_not_found("not found dir name of dir_id: {}", key);
            }
        }
        return {};
    }

    [[nodiscard]] size_t PackFileHandle::PackFileMetaSerializedSize() const { return pack_file_meta_.SerializedSize(); }

    std::expected<EntryMeta, ErrorStatus> PackFileHandle::ProcessDirEntry(const EntryType entry_type,
                                                                          const struct stat& file_stat)
    {
        EntryMeta file_meta{};
        file_meta.entry_type = entry_type;

        if (entry_type == EntryType::REGULAR)
        {
            const uint64_t file_size = static_cast<uint64_t>(file_stat.st_size);
            file_meta.file_orig_size = file_size;
            pack_file_meta_.file_data_original_size += file_size;
            pack_file_meta_.file_count += 1;
        }

#if defined(__APPLE__)
        const struct timespec mtime = file_stat.st_mtimespec;
#else
        const struct timespec mtime = file_stat.st_mtim;
#endif
        file_meta.modify_time = static_cast<long long>(mtime.tv_sec) * 1000 + mtime.tv_nsec / 1000000;
        file_meta.permissions = static_cast<uint32_t>(file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
        const std::string& owner_name = GetUserName(static_cast<uint32_t>(file_stat.st_uid));
        const std::string& group_name = GetGroupName(static_cast<uint32_t>(file_stat.st_gid));
        std::snprintf(file_meta.owner_name, std::size(file_meta.owner_name), "%s", owner_name.c_str());
        std::snprintf(file_meta.group_name, std::size(file_meta.group_name), "%s", group_name.c_str());
        file_meta.block_id = 0;
        file_meta.offset = 0;
        return file_meta;
    }

    Status PackFileHandle::ScanDirectory(const std::string& root_path, const bool recursive, const bool filter_junk)
    {
        const fs::path root(root_path);

        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            return error_io("'{}' is not a readable directory: {}", root_path,
                            ec ? ec.message() : std::string("not a directory"));
        }

        // Open the iterator before touching any metadata, so a bad root cannot leave a
        // half-built entry behind.
        fs::directory_iterator it(root, ec);
        if (ec)
        {
            return error_io("fail to open directory '{}': {}", root_path, ec.message());
        }

        struct ScanEntry
        {
            fs::path path;
            EntryType type;
            // Offset of the file name in path.native(); avoids storing a second
            // string per entry. AddFile() copies the name immediately, so a view
            // into the path is enough.
            size_t name_offset;
            struct stat file_stat;

            [[nodiscard]] std::string_view name() const
            {
                return std::string_view(path.native()).substr(name_offset);
            }
        };

        // Children of this directory all have this id as their parent; no need to
        // resolve it again from the full path for every entry.
        const entry_id_t current_dir_id = dir_id_;
        AddDir(dir_id_, root_path);
        dir_id_++;

        std::vector<ScanEntry> entries;
        const fs::directory_iterator end;
        while (it != end)
        {
            const fs::directory_entry& e = *it;
            bool take = false;
            EntryType type = EntryType::REGULAR;
            struct stat file_stat{};
            if (!(filter_junk && is_skip_entry(e)) && ::lstat(e.path().c_str(), &file_stat) == 0)
            {
                if (S_ISDIR(file_stat.st_mode))
                {
                    type = EntryType::DIRECTORY;
                    take = true;
                }
                else if (S_ISREG(file_stat.st_mode))
                {
                    type = EntryType::REGULAR;
                    take = true;
                }
            }
            if (take)
            {
                const std::string& native = e.path().native();
                const size_t sep = native.find_last_of('/');
                entries.push_back({e.path(), type, sep == std::string::npos ? 0 : sep + 1, file_stat});
            }
            it.increment(ec);
            if (ec)
            {
                return error_io("fail to enumerate directory '{}': {}", root_path, ec.message());
            }
        }

        // Directories first, then by name. Both keys are computed once per entry; the
        // previous comparator rebuilt two fs::path objects on every comparison.
        std::ranges::sort(entries,
                          [](const ScanEntry& a, const ScanEntry& b)
                          {
                              const bool a_dir = a.type == EntryType::DIRECTORY;
                              const bool b_dir = b.type == EntryType::DIRECTORY;
                              if (a_dir != b_dir)
                              {
                                  return a_dir;
                              }
                              return a.name() < b.name();
                          });

        for (const auto& s : entries)
        {
            const std::string& entry_path = s.path.native();
            auto result = ProcessDirEntry(s.type, s.file_stat);
            if (!result)
            {
                return std::unexpected(result.error());
            }
            const EntryMeta& meta = result.value();

            const entry_id_t parent_dir_id = current_dir_id;
            if (s.type == EntryType::DIRECTORY)
            {
                AddDir(dir_id_, entry_path, parent_dir_id, meta);
                if (recursive)
                {
                    if (auto scan_status = ScanDirectory(entry_path, true, filter_junk); !scan_status)
                    {
                        return scan_status;
                    }
                }
            }
            else
            {
                AddFile(file_id_, s.name(), parent_dir_id, meta);
                file_id_++;
            }
        }
        return {};
    }


    Status PackFileHandle::RenderDirListing(std::ostream& out, entry_id_t dir_id, const std::string& prefix,
                                            const OutputMode output_mode, bool human_readable, bool has_header) const
    {
        auto dir_name_res = GetDirPathByDirId(dir_id);
        if (!dir_name_res)
        {
            return error_io("fail to get dir name, dir_id: {}", dir_id);
        }
        const std::string_view dir_name = dir_name_res.value();
        if (has_header)
        {
            if (output_mode == OutputMode::TREE)
            {
                out << kBlueStart << dir_name << kColorReset << '\n';
            }
            else if (output_mode == OutputMode::TSV)
            {
                out << tsv::kTsvHeaderName;
            }
            has_header = false;
        }
        const auto& sub_dirs = GetChildDirIdsByDirId(dir_id);
        const auto& sub_files = GetChildFileIdsByDirId(dir_id);
        struct TreeEntry
        {
            std::string name;
            bool is_directory;
            entry_id_t id;
        };
        std::vector<TreeEntry> entries;
        entries.reserve(sub_dirs.size() + sub_files.size());
        for (entry_id_t sub_dir_id : sub_dirs)
        {
            auto dir_name_r = GetDirPathByDirId(sub_dir_id);
            if (!dir_name_r)
            {
                return error_io("fail to get sub‑dir name, dir_id: {}", sub_dir_id);
            }
            std::filesystem::path path_obj(dir_name_r.value());
            entries.emplace_back(path_obj.filename().string(), true, sub_dir_id);
        }
        for (entry_id_t sub_file_id : sub_files)
        {
            auto name_res = GetFileNameByFileId(sub_file_id);
            if (!name_res)
            {
                return error_io("fail to get file name, file_id: {}", sub_file_id);
            }
            entries.emplace_back(std::string(name_res.value()), false, sub_file_id);
        }
        std::ranges::sort(entries, [](const TreeEntry& a, const TreeEntry& b) { return a.name < b.name; });
        if (output_mode == OutputMode::TREE)
        {
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& entry = entries[i];
                const bool is_last = (i == entries.size() - 1);
                out << prefix << (is_last ? "└── " : "├── ");
                if (entry.is_directory)
                {
                    out << kBlueStart;
                }
                out << entry.name << kColorReset << '\n';
                if (entry.is_directory)
                {
                    std::string new_prefix = prefix + (is_last ? "    " : "│   ");
                    auto status =
                        RenderDirListing(out, entry.id, new_prefix, OutputMode::TREE, human_readable, has_header);
                    if (!status)
                        return status;
                }
            }
        }
        else
        {
            ListColWidths col_widths{0, 0, 0};
            if (output_mode == OutputMode::LIST)
            {
                for (entry_id_t sub_file_id : sub_files)
                {
                    if (auto res = GetListWidthByFileId(sub_file_id))
                    {
                        col_widths.owner_name_width =
                            std::max(col_widths.owner_name_width, res.value().owner_name_width);
                        col_widths.group_name_width =
                            std::max(col_widths.group_name_width, res.value().group_name_width);
                        col_widths.file_size_width = std::max(col_widths.file_size_width, res.value().file_size_width);
                    }
                    else
                    {
                        return std::unexpected(res.error());
                    }
                }
            }
            for (const auto& entry : entries)
            {
                if (!entry.is_directory)
                {
                    auto it = pack_file_meta_.file_entry_records.find(entry.id);
                    if (it != pack_file_meta_.file_entry_records.end())
                    {
                        const auto& file_meta = it->second.entry_meta;
                        DirFileRef dir_file_ref = {dir_id, dir_name, entry.id, entry.name};
                        // std::string file_path = std::format("{}/{}", dir_name, entry.name);
                        file_meta.FormatOutput(out, dir_file_ref, output_mode, human_readable, col_widths);
                    }
                }
                else
                {
                    auto status = RenderDirListing(out, entry.id, prefix, output_mode, human_readable, has_header);
                    if (!status)
                        return status;
                }
            }
        }

        return {};
    }

} // namespace ctar
