#ifndef CTAR_FORMAT_H
#define CTAR_FORMAT_H
#include <cassert>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <ostream>
#include <ranges>
#include <string>
#include <unordered_map>

#include "options.h"
#include "sorted_map.h"
#include "src/utils/status.h"


namespace ctar
{
    namespace fs = std::filesystem;
    constexpr std::string_view blue_start = "\033[34m";
    constexpr std::string_view color_reset = "\033[0m";

    using entry_id_t = uint64_t;
    constexpr entry_id_t kRootDirId = 1;
    constexpr entry_id_t kFileBeginId = 1;


    enum class EntryType : uint16_t
    {
        REGULAR = 0,
        DIRECTORY = 1,
    };

    enum class OutputMode : uint8_t
    {
        TREE = 0,
        LIST = 1,
        TSV = 2,
    };

    inline constexpr std::string_view kTsvMetaHeader =
        "block_id\tdir_id\tfile_id\tpermissions\towner\tgroup\tmodify_time\toffset\toriginal_size\tcompressed_"
        "size\tdir_name\tfile_name\n";

    constexpr uint8_t kListColumnSize = 6;

    constexpr uint8_t kTsvColumnSize = 12;

    struct Version
    {
        uint8_t major{};
        uint8_t minor{};
        uint8_t patch{};
        uint8_t reserved{};

        constexpr Version() noexcept = default;

        constexpr Version(uint8_t maj, uint8_t min, uint8_t pat) noexcept : major(maj), minor(min), patch(pat) {}

        [[nodiscard]] constexpr uint32_t Encode() const noexcept
        {
            return (static_cast<uint32_t>(reserved) << 24U) | (static_cast<uint32_t>(patch) << 16U) |
                (static_cast<uint32_t>(minor) << 8U) | static_cast<uint32_t>(major);
        }

        [[nodiscard]] static constexpr Version Decode(const uint32_t ver) noexcept
        {
            Version v;
            v.reserved = static_cast<uint8_t>((ver >> 24U) & 0xFFU);
            v.patch = static_cast<uint8_t>((ver >> 16U) & 0xFFU);
            v.minor = static_cast<uint8_t>((ver >> 8U) & 0xFFU);
            v.major = static_cast<uint8_t>(ver & 0xFFU);
            return v;
        }

        [[nodiscard]] std::string to_string() const { return std::format("{}.{}.{}", major, minor, patch); }
    };

    inline Version cur_version{0, 0, 1};


    /// @brief Stores calculated maximum column widths for LIST formatted directory output
    struct ListColWidths
    {
        size_t owner_name_width;
        size_t group_name_width;
        size_t file_size_width;
    };


    /**
     * @brief Non‑owning reference pair of directory and its child file entry.
     * @note String views reference external buffer; do not persist beyond buffer lifetime.
     */
    struct DirFileRef
    {
        entry_id_t dir_id;
        std::string_view dir_name;
        entry_id_t file_id;
        std::string_view file_name;
    };

    struct EntryMeta
    {
        EntryType entry_type;
        uint32_t permissions;
        uint64_t block_id;
        char owner_name[64];
        char group_name[64];
        // modify_time: fs::last_write_time represented as unix epoch milliseconds
        long long modify_time;
        uint64_t offset;
        uint64_t file_orig_size;
        uint64_t file_compressed_size;

        /// @brief Format  output stream
        /// @param os output stream
        /// @param entry_path Path starting from designated base directory
        /// @param mode output format mode
        /// @param human_readable enable human‑friendly number formatting
        /// @param col_widths control the output width when OutputMode is LIST
        void FormatOutput(std::ostream& os, const DirFileRef& dir_file_ref, OutputMode mode, bool human_readable,
                          const ListColWidths& col_widths) const;
    };


    inline size_t SafeStrLen(const char* buf, size_t buf_size) noexcept
    {
        if (buf == nullptr || buf_size == 0)
        {
            return 0;
        }
        for (size_t i = 0; i < buf_size; ++i)
        {
            if (buf[i] == '\0')
            {
                return i;
            }
        }
        return buf_size;
    }

    struct Handle
    {
        uint64_t offset;
        uint64_t len;
    };

    /// @brief Filesystem entry record, represents either file or directory    struct EntryRecord
    struct EntryRecord
    {
        /// Handle pointing to path‑name string (file name or directory path)
        Handle name_handle;
        /// Metadata for entry, valid for both file and directory
        EntryMeta entry_meta;
    };

    struct FileStats
    {
        uint64_t total_file_count = 0;
        uint64_t total_original_size = 0;
        uint64_t total_compressed_size = 0;
        uint64_t padding_size = 0;
    };

    struct PackFileHeader
    {
        uint32_t magic_number = MagicNumber;
        Version version = cur_version;
        uint64_t file_meta_size = 0;
        PackFileHeader() = default;
        void Serialize(std::string* dst) const;
        Status Deserialize(const char*& ptr);
        [[nodiscard]] size_t SerializedSize() const;
    };

    struct PackFileMeta
    {
        uint64_t total_file_count = 0;
        uint64_t total_original_size = 0;
        uint64_t total_compressed_size = 0;
        uint64_t padding_size = 0;
        std::string dir_name_data;
        std::string file_name_data;
        SortedMap<entry_id_t, EntryRecord> dir_entry_records;
        SortedMap<entry_id_t, EntryRecord> file_entry_records;
        SortedMap<entry_id_t, std::vector<entry_id_t>> dir_id_to_child_dir_ids;
        SortedMap<entry_id_t, std::vector<entry_id_t>> dir_id_to_child_file_ids;
        SortedMap<entry_id_t, std::vector<uint64_t>> dir_id_to_child_file_sizes;
        PackFileMeta();
        void Clear();
        [[nodiscard]] size_t SerializedSize() const;
        void Serialize(std::string* dst) const;
        void Deserialize(const char*& ptr);
    };

    class PackFileHandle
    {
    private:
        std::unordered_map<std::string, entry_id_t> dir_path_to_dir_id_;
        uint64_t dir_id_ = kRootDirId;
        uint64_t file_id_ = kFileBeginId;
        PackFileMeta pack_file_meta_;

    public:
        /// @brief Scan host filesystem directory, import entries into pack metadata
        /// @param root_path designated host‑system root directory for scanning
        /// @param recursive whether scan sub‑directories recursively
        /// @param filter_junk whether filter out junk files(e.g. .DS_Store)
        /// @return Status::Ok on success
        Status ScanDirectory(const std::string& root_path, bool recursive = true, bool filter_junk = true);

        /// @brief Clear all pack metadata, reset to empty state
        void Clear();

        /// @brief Serialize PackFileMeta to output buffer dst
        /// @param dst output string buffer, content will be appended
        void SerializePackFileMeta(std::string* dst) const;

        /// @brief Deserialize PackFileMeta from raw buffer, advance ptr on success
        /// @param[in,out] ptr input buffer pointer; advanced past metadata when return ok
        /// @return Status::Ok on success, error on corrupt / insufficient data
        Status DeserializePackFileMeta(const char*& ptr);

        /// @brief Calculate total serialized byte size of PackFileMeta
        /// @return total bytes required for serialization
        [[nodiscard]] size_t PackFileMetaSerializedSize() const;

        /// @brief Get total count of files
        /// @return total file entries count
        [[nodiscard]] uint64_t TotalFileCnt() const
        {
#ifndef NDEBUG
            uint64_t real_sum = 0;
            for (const auto& val : pack_file_meta_.dir_id_to_child_file_ids | std::views::values)
            {
                real_sum += val.size();
            }
            assert(real_sum == pack_file_meta_.total_file_count && "total_file_count cache dirty");
#endif
            return pack_file_meta_.total_file_count;
        }

        /// @brief Get total payload file size sum of all files
        /// @return accumulated file bytes
        [[nodiscard]] uint64_t TotalFileSize() const noexcept
        {
#ifndef NDEBUG
            uint64_t real_sum = 0;
            for (const auto& vec : pack_file_meta_.dir_id_to_child_file_sizes | std::views::values)
            {
                for (uint64_t file_byte : vec)
                {
                    real_sum += file_byte;
                }
            }
            assert(real_sum == pack_file_meta_.total_original_size && "total_file_size_ cache dirty");
#endif
            return pack_file_meta_.total_original_size;
        }

        /// @brief Get file stats of packed file
        /// @return File statistics summary
        ///         - total_file_count: number of stored files
        ///         - total_original_size: original uncompressed total size
        ///         - total_compressed_size: compressed payload total size
        ///         - padding_size: alignment padding bytes
        [[nodiscard]] FileStats GetFileStats() const
        {
            return {pack_file_meta_.total_file_count, pack_file_meta_.total_original_size,
                    pack_file_meta_.total_compressed_size, pack_file_meta_.padding_size};
        }

        /// @brief Add entry metadata into map, append name string to data buffer
        /// @param key unique entry id
        /// @param name entry dir/file name
        /// @param data_buf string buffer stores name content
        /// @param map entry map indexed by key
        /// @param file_meta entry metadata
        void AddEntry(entry_id_t key, std::string_view name, std::string* data_buf,
                      SortedMap<entry_id_t, EntryRecord>& map, const EntryMeta& file_meta);

        /// @brief Add directory entry into pack metadata
        /// @param dir_id directory unique id
        /// @param dir_path Path starting from designated base directory
        /// @param parent_dir_id parent directory id
        /// @param file_meta directory metadata
        void AddDir(entry_id_t dir_id, std::string_view dir_path, entry_id_t parent_dir_id = 0,
                    const EntryMeta& file_meta = {});

        /// @brief Add a file entry into metadata index
        /// @param key unique file id for this file
        /// @param file_name base name of file (not full path)
        /// @param parent_dir_id parent directory id this file belongs to
        /// @param file_meta detailed file metadata(size, offset etc.)
        void AddFile(entry_id_t key, std::string_view file_name, entry_id_t parent_dir_id, const EntryMeta& file_meta);

        /// @brief Lookup dir_path by directory id
        /// @param dir_id directory unique id
        /// @return Ok(dir_path) on success, Error if id not found
        /// @note Returned string_view references internal object storage, remains valid while object alive
        [[nodiscard]] ResultStatus<std::string_view> GetDirPathByDirId(entry_id_t dir_id) const;

        /// @brief Lookup directory id by dir_path
        /// @param dir_path Path starting from designated base directory
        /// @return Ok(dir_id) on success, Error if path not found
        [[nodiscard]] ResultStatus<entry_id_t> GetDirIdByDirPath(const std::string& dir_path) const;


        /// @brief Get base file‑name (without path) by file id
        /// @param key target file id
        /// @return Ok: file base‑name string_view; Error: id does not exist
        [[nodiscard]] ResultStatus<std::string_view> GetFileNameByFileId(entry_id_t key) const;


        /// @brief Get formatted list‑view column widths by entry file id
        /// @param key entry unique file id to lookup
        /// @return Result contains ListColWidths on success; error status on lookup failure
        [[nodiscard]] ResultStatus<ListColWidths> GetListWidthByFileId(entry_id_t key) const;

        /// @brief Look up parent directory id by absolute file/directory path
        /// @param entry_path absolute path string view
        /// @return ResultStatus: ok -> parent directory id of given path;
        ///         error when path does not exist or is root directory(no parent).
        ResultStatus<entry_id_t> GetParentDirIdByPath(std::string_view entry_path);

        /// @brief Get direct‑child directory ids under given parent directory
        /// @param parent_dir_id id of target parent directory
        /// @return const reference to list of direct‑child directory ids; empty if directory not exists
        [[nodiscard]] const std::vector<entry_id_t>& GetChildDirIdsByDirId(entry_id_t parent_dir_id) const;

        /// @brief Get direct‑child file ids under given parent directory
        /// @param parent_dir_id target parent directory id
        /// @return const reference to list of direct child file ids; empty if dir not found
        [[nodiscard]] const std::vector<entry_id_t>& GetChildFileIdsByDirId(entry_id_t parent_dir_id) const;

        /// @brief Get mapping from directory id to its direct child file ids
        /// @return const reference: key = parent directory id, value = list of direct‑child file ids
        const SortedMap<entry_id_t, std::vector<entry_id_t>>& GetDirIdToChildFileIds() const
        {
            return pack_file_meta_.dir_id_to_child_file_ids;
        }

        /// @brief Get read‑only view of all directory entry records
        /// @return const reference to directory entry records map
        const SortedMap<entry_id_t, EntryRecord>& GetDirEntryRecords() const
        {
            return pack_file_meta_.dir_entry_records;
        }


        /// @brief Get child file sizes under specified parent directory
        /// @param parent_dir_id parent directory entry id
        /// @return const reference to vector of child file byte‑sizes; empty if no children or dir not found
        [[nodiscard]] const std::vector<uint64_t>& GetFileSizeByDirId(entry_id_t parent_dir_id) const;

        /// @brief Set block offset and block_id for a file entry
        /// @param file_id target file entry id
        /// @param block_id block identifier
        /// @param offset block payload offset inside pack file
        void SetFileEntryMeta(entry_id_t file_id, uint64_t block_id, uint64_t offset, uint64_t compressed_size);

        /// @brief Set actual compressed payload bytes, compute block‑region padding bytes
        /// @param compressed_bytes Real size of compressed payload data
        /// @param block_region_size Total size of block region (payload + padding)
        void SetCompressedAndPaddingBytes(const uint64_t compressed_bytes, const uint64_t block_region_size)
        {
            pack_file_meta_.total_compressed_size = compressed_bytes;
            pack_file_meta_.padding_size = block_region_size - compressed_bytes;
        }

        /// @brief Process filesystem directory entry, extract metadata from OS
        /// @param entry filesystem directory entry from std::filesystem
        /// @return EntryMeta on success, ErrorStatus on failure(permission denied, stat failed etc.)
        std::expected<EntryMeta, ErrorStatus> ProcessDirEntry(const fs::directory_entry& entry);

        /// @brief Render directory entries into specified format and write to output stream
        /// @param out output stream for TREE / LIST / TSV output
        /// @param dir_id start directory entry id, default is root directory id(kRootDirId)
        /// @param prefix string prefix used for tree indent formatting; for internal recursive‑only use
        /// @param output_mode select output format
        ///        - TREE: tree‑style display, shows files and folders with indent hierarchy
        ///        - LIST: flat one‑line‑per‑entry output
        ///        - TSV: tab‑separated structured table
        /// @param human_readable format numeric sizes/timestamps for human reading
        /// @param has_header whether output TSV table header line; only takes effect for TSV mode
        /// @return Status::Ok on complete render, error on invalid dir_id
        Status RenderDirListing(std::ostream& out, entry_id_t dir_id = kRootDirId, const std::string& prefix = "",
                                OutputMode output_mode = OutputMode::TREE, bool human_readable = false,
                                bool has_header = false) const;
    };

    struct DataBlockMeta
    {
        // block id of data blocks, start from 0
        uint64_t block_id = 0;
        // the dir_id of this data block
        entry_id_t dir_id = 0;
        // Offset relative to the start of the data block.
        // For example: block‑0 has size 4K, its block_file_offset is 0, then block_file_offset of block‑1  is 4K.
        uint64_t block_file_offset = 0;
        // Size of block (without padding). If compression is enabled, this is the compressed size.
        uint64_t payload_size = 0;
        // Size of block with padding
        uint64_t block_size = 0;
        // xxhash of data block file, not calculate at this version
        uint64_t hash_value = 0;
        // the file ids of the files in this data block
        std::vector<entry_id_t> file_ids = {};
        // the original file size of the files in this data block
        std::vector<uint64_t> file_orig_sizes = {};
        // the compressed file size of the files in this data block
        std::vector<uint64_t> file_compressed_sizes = {};
        // the compressed type of the files in this data block
        std::vector<CompressionType> file_compressed_types = {};
        // the offset (relation to this block) of each files in this data block
        // For example: file-0 has size 256, file-1 has size 300, file_offsets = {0,256};
        std::vector<uint64_t> file_offsets = {};
        // the xxhash of the original file data in this data block
        std::vector<uint64_t> file_hashes = {};


        // Temporary helper metadata for block grouping
        struct DirStats
        {
            // Directory path corresponding to this block
            std::string_view dir_path;
            // Maximum file size among files inside this directory
            uint64_t max_file_size;
        } dir_stats{{}, 0};

        DataBlockMeta() = default;
        [[nodiscard]] size_t SerializedSize() const;
        friend class BlockStore;
        void Serialize(std::string* dst) const;
        Status Deserialize(const char*& ptr);
        [[nodiscard]] std::string DebugString() const;
    };
}; // namespace ctar
#endif // CTAR_FORMAT_H
