#include <grp.h>
#include <gtest/gtest.h>
#include <pwd.h>
#include "gmock/gmock-matchers.h"
#include "src/core/file_format.h"
#include "test/tools/fixture/blob_store_fixture.h"
#include "test/tools/test_helpers.h"
namespace ctar
{
    using ::testing::ElementsAre;


    TEST(Version, SerializeDeserialize)
    {
        constexpr uint8_t major = 1;
        constexpr uint8_t minor = 2;
        constexpr uint8_t patch = 3;
        constexpr Version version(major, minor, patch);
        constexpr uint32_t version_number = version.Encode();
        constexpr Version deser_version = Version::Decode(version_number);
        ASSERT_EQ(deser_version.major, major);
        ASSERT_EQ(deser_version.minor, minor);
        ASSERT_EQ(deser_version.patch, patch);
    }

    TEST(PackFileHeader, SerializeDeserialize)
    {
        uint64_t file_meta_size = 4096;
        PackFileHeader file_header;
        file_header.file_meta_size = file_meta_size;
        ASSERT_EQ(file_header.magic_number, MagicNumber);
        ASSERT_EQ(file_header.version.Encode(), cur_version.Encode());
        ASSERT_EQ(file_header.file_meta_size, file_meta_size);
        std::string buf;
        file_header.Serialize(&buf);

        PackFileHeader deser_file_header{};
        const char* p = buf.data();

        Status s = deser_file_header.Deserialize(p);
        ASSERT_TRUE(s);
        ASSERT_EQ(deser_file_header.magic_number, MagicNumber);
        ASSERT_EQ(deser_file_header.version.Encode(), cur_version.Encode());
        ASSERT_EQ(deser_file_header.file_meta_size, file_meta_size);
    }


    TEST(DataBlobMeta, SerializeDeserializeWithData)
    {
        DataBlockMeta src;
        src.hash_value = 0xabcd;
        src.block_id = 1;
        src.dir_id = 2;
        src.file_ids = {10, 11, 12};
        src.file_orig_sizes = {100, 200, 300};
        src.file_compressed_sizes = {50, 100, 150};
        src.file_compressed_types = {CompressionType::kLZ4, CompressionType::kLZ4HC, CompressionType::kNone};
        src.file_offsets = {0, 100, 200};
        src.file_hashes = {0x12, 0x34, 0x56};
        src.block_file_offset = 0;
        src.block_size = 4096;
        std::string buf;
        src.Serialize(&buf);
        const size_t write_len = buf.size();
        const size_t serialize_bytes = src.SerializedSize();
        EXPECT_EQ(write_len, serialize_bytes) << "SerializedSize () mismatch actual write bytes";

        DataBlockMeta dst;
        const char* p = buf.data();
        auto status = dst.Deserialize(p);
        ASSERT_TRUE(status);
        const size_t deserialize_bytes = p - buf.data();
        EXPECT_EQ(write_len, deserialize_bytes) << "Deserialization consumed different bytes, pointer drift!";
        EXPECT_EQ(dst.hash_value, src.hash_value);
        EXPECT_EQ(dst.block_id, src.block_id);
        EXPECT_EQ(dst.dir_id, src.dir_id);
        EXPECT_EQ(dst.file_ids, src.file_ids);
        EXPECT_EQ(dst.file_orig_sizes, src.file_orig_sizes);
        EXPECT_EQ(dst.file_compressed_sizes, src.file_compressed_sizes);
        EXPECT_EQ(dst.file_compressed_types, src.file_compressed_types);
        EXPECT_EQ(dst.file_offsets, src.file_offsets);
        EXPECT_EQ(dst.file_hashes, src.file_hashes);
        EXPECT_EQ(dst.block_file_offset, src.block_file_offset);
        EXPECT_EQ(dst.block_size, src.block_size);
    }


    TEST(DataBlobMeta, SerializeDeserializeEmptyArrays)
    {
        DataBlockMeta src;
        src.hash_value = 0xdeadbeef;
        src.block_id = 5;
        src.dir_id = 7;
        src.file_ids.clear();
        src.file_orig_sizes.clear();
        src.file_compressed_sizes.clear();
        src.file_compressed_types.clear();
        src.file_offsets.clear();
        src.file_hashes.clear();
        src.block_file_offset = 8192;
        src.block_size = 65536;
        std::string buf;
        src.Serialize(&buf);
        const size_t write_len = buf.size();
        const size_t serialize_bytes = src.SerializedSize();
        EXPECT_EQ(write_len, serialize_bytes);
        DataBlockMeta dst;
        const char* p = buf.data();
        auto status = dst.Deserialize(p);
        ASSERT_TRUE(status);
        const size_t deserialize_bytes = p - buf.data();
        EXPECT_EQ(write_len, deserialize_bytes);

        EXPECT_EQ(dst.hash_value, src.hash_value);
        EXPECT_EQ(dst.block_id, src.block_id);
        EXPECT_EQ(dst.dir_id, src.dir_id);
        EXPECT_TRUE(dst.file_ids.empty());
        EXPECT_TRUE(dst.file_orig_sizes.empty());
        EXPECT_TRUE(dst.file_compressed_sizes.empty());
        EXPECT_TRUE(dst.file_compressed_types.empty());
        EXPECT_TRUE(dst.file_offsets.empty());
        EXPECT_TRUE(dst.file_hashes.empty());
        EXPECT_EQ(dst.block_file_offset, src.block_file_offset);
        EXPECT_EQ(dst.block_size, src.block_size);
    }

    TEST(DataBlobMeta, SerializeDeserializeSingleElement)
    {
        DataBlockMeta src;
        src.block_id = 10;
        src.dir_id = 11;
        src.file_ids = {99};
        src.file_orig_sizes = {4096};
        src.file_compressed_sizes = {1024};
        src.file_compressed_types = {CompressionType::kLZ4};
        src.file_offsets = {0};
        src.file_hashes = {0x8888};
        src.block_file_offset = 4096;
        src.block_size = 1024 * 1024;
        src.hash_value = 0x5555;
        std::string buf;
        src.Serialize(&buf);
        const size_t write_len = buf.size();
        const size_t serialize_bytes = src.SerializedSize();
        EXPECT_EQ(write_len, serialize_bytes);
        DataBlockMeta dst;
        const char* p = buf.data();
        auto status = dst.Deserialize(p);
        ASSERT_TRUE(status);
        const size_t deserialize_bytes = p - buf.data();
        EXPECT_EQ(write_len, deserialize_bytes);
        EXPECT_EQ(dst.file_ids, src.file_ids);
        EXPECT_EQ(dst.file_orig_sizes, src.file_orig_sizes);
        EXPECT_EQ(dst.file_compressed_sizes, src.file_compressed_sizes);
        EXPECT_EQ(dst.file_compressed_types, src.file_compressed_types);
        EXPECT_EQ(dst.file_offsets, src.file_offsets);
        EXPECT_EQ(dst.file_hashes, src.file_hashes);
    }


    TEST(FileBlobMetaTest, SerializeDeserializeWithData)
    {
        PackFileMeta src;

        src.dir_name_data = "/test/dir_name";
        entry_id_t dir_id = 1;
        std::string file1 = "hello.txt";
        entry_id_t file1_id = 1;
        uint64_t file1_size = 4096;

        std::string file2 = "readme.md";
        entry_id_t file2_id = 2;
        uint64_t file2_size = 8192;

        src.file_name_data = file1 + file2;

        EntryRecord dir_entry1{};
        dir_entry1.name_handle.offset = 0;
        dir_entry1.name_handle.len = src.dir_name_data.size();
        src.dir_entry_records.put(1, dir_entry1);

        EntryRecord file_entry1{};
        file_entry1.name_handle.offset = 0;
        file_entry1.name_handle.len = file1.size();
        src.file_entry_records.put(file1_id, file_entry1);

        EntryRecord file_entry2{};
        file_entry2.name_handle.offset = file1.size();
        file_entry2.name_handle.len = file2.size();
        src.file_entry_records.put(file2_id, file_entry2);

        src.dir_id_to_child_dir_ids.put(dir_id, std::vector<entry_id_t>{});

        src.dir_id_to_child_file_ids.put(dir_id, std::vector<entry_id_t>{file1_id, file2_id});
        src.dir_id_to_child_file_sizes.put(dir_id, std::vector<uint64_t>{file1_size, file2_size});

        // Serialize
        std::string buffer;
        src.Serialize(&buffer);
        const size_t write_len = buffer.size();
        const size_t calc_len = src.SerializedSize();
        EXPECT_EQ(write_len, calc_len) << "SerializedSize () != actual serialized byte count, size mismatch";

        // Deserialize
        PackFileMeta dst;
        const char* ptr = buffer.data();
        dst.Deserialize(ptr);
        const size_t consumed_bytes = ptr - buffer.data();
        EXPECT_EQ(write_len, consumed_bytes) << "Pointer drift detected, deserialization consumed wrong bytes";

        // data check
        EXPECT_EQ(dst.dir_name_data, src.dir_name_data);
        EXPECT_EQ(dst.file_name_data, src.file_name_data);
        EXPECT_EQ(dst.dir_entry_records.size(), src.dir_entry_records.size());
        EXPECT_EQ(dst.file_entry_records.size(), src.file_entry_records.size());
        auto src_subdir = src.dir_id_to_child_dir_ids.find(1)->second;
        auto dst_subdir = dst.dir_id_to_child_dir_ids.find(1)->second;
        EXPECT_EQ(dst_subdir, src_subdir);
        auto src_subfile = src.dir_id_to_child_file_ids.find(1)->second;
        auto dst_subfile = dst.dir_id_to_child_file_ids.find(1)->second;
        EXPECT_EQ(dst_subfile, src_subfile);
        auto src_sizes = src.dir_id_to_child_file_sizes.find(1)->second;
        auto dst_sizes = dst.dir_id_to_child_file_sizes.find(1)->second;
        EXPECT_EQ(dst_sizes, src_sizes);
    }


    TEST(FileBlobMetaTest, SerializeDeserializeEmptyContainers)
    {
        PackFileMeta src;
        src.dir_name_data.clear();
        src.file_name_data.clear();

        std::string buffer;
        src.Serialize(&buffer);
        const size_t write_len = buffer.size();
        const size_t calc_len = src.SerializedSize();
        EXPECT_EQ(write_len, calc_len);
        PackFileMeta dst;
        const char* ptr = buffer.data();
        dst.Deserialize(ptr);
        size_t consumed = ptr - buffer.data();
        EXPECT_EQ(write_len, consumed);
        EXPECT_TRUE(dst.dir_name_data.empty());
        EXPECT_TRUE(dst.file_name_data.empty());
        EXPECT_TRUE(dst.dir_entry_records.empty());
        EXPECT_TRUE(dst.file_entry_records.empty());
        EXPECT_TRUE(dst.dir_id_to_child_dir_ids.empty());
        EXPECT_TRUE(dst.dir_id_to_child_file_ids.empty());
        EXPECT_TRUE(dst.dir_id_to_child_file_sizes.empty());
    }


    void AssertEntryName(const auto& map, const std::string& name_data, entry_id_t id, std::string_view expect,
                         std::string_view type)
    {
        auto it = map.find(id);
        if (it == map.end())
        {
            FAIL() << "not found target " << type << " id: " << id;
        }
        const auto& h = it->second.name_handle;
        ASSERT_EQ(name_data.substr(h.offset, h.len), expect);
    }

    TEST(FileBlobMetaTest, SerializeDeserializeMultipleParentEntries)
    {
        PackFileMeta src;
        std::string dir1 = "dir1";
        entry_id_t dir1_id = 1;
        std::string file1 = "file1_1";
        entry_id_t file1_id = 1;
        uint64_t file1_size = 100;
        std::string file2 = "file1_2";
        entry_id_t file2_id = 2;
        uint64_t file2_size = 200;

        std::string dir2 = "dir1/dir2";
        entry_id_t dir2_id = 2;
        std::string file3 = "file2_1";
        entry_id_t file3_id = 3;
        uint64_t file3_size = 300;

        std::string file4 = "file2_2";
        entry_id_t file4_id = 4;
        uint64_t file4_size = 400;

        auto dir_file_meta = EntryMeta{EntryType::DIRECTORY, 0644, 0, "root", "staff", 3600000, 0, 0,0};
        auto dir1_entry = EntryRecord{{0, dir1.size()}, dir_file_meta};
        auto dir2_entry = EntryRecord{{dir1.size(), dir2.size()}, dir_file_meta};

        auto file1_file_meta = EntryMeta{EntryType::REGULAR, 0644, 0, "root", "staff", 3600000, 0, file1_size,file1_size};
        auto file1_entry = EntryRecord{{0, file1.size()}, file1_file_meta};

        auto file2_file_meta = EntryMeta{EntryType::REGULAR, 0644, 0, "root", "staff", 3600000, file1_size, file2_size,file2_size};
        auto file2_entry = EntryRecord{{file1.size(), file2.size()}, file2_file_meta};

        auto file3_file_meta =
            EntryMeta{EntryType::REGULAR, 0644, 0, "root", "staff", 3600000, file1_size + file2_size, file3_size,file3_size};
        auto file3_entry = EntryRecord{{file1.size() + file2.size(), file3.size()}, file3_file_meta};

        auto file4_file_meta = EntryMeta{
            EntryType::REGULAR, 0644, 0, "root", "staff", 3600000, file1_size + file2_size + file3_size, file4_size,file4_size};
        auto file4_entry = EntryRecord{{file1.size() + file2.size() + file3.size(), file4.size()}, file4_file_meta};

        ASSERT_EQ(PermBitsToString(file1_entry.entry_meta.permissions), "rw-r--r--");
        ASSERT_EQ(FormatTime(file1_entry.entry_meta.modify_time / 1000), "Jan 01  1970");

        src.dir_name_data = dir1 + dir2;
        src.file_name_data = file1 + file2 + file3 + file4;

        src.dir_entry_records.put(dir1_id, dir1_entry);
        src.dir_entry_records.put(dir2_id, dir2_entry);

        src.file_entry_records.put(file1_id, file1_entry);
        src.file_entry_records.put(file2_id, file2_entry);
        src.file_entry_records.put(file3_id, file3_entry);
        src.file_entry_records.put(file4_id, file4_entry);

        src.dir_id_to_child_dir_ids.put(dir1_id, {dir2_id});
        src.dir_id_to_child_dir_ids.put(dir2_id, {});
        src.dir_id_to_child_file_ids.put(dir1_id, {file1_id, file2_id});
        src.dir_id_to_child_file_sizes.put(dir2_id, {file3_id, file4_id});

        std::string buf;
        src.Serialize(&buf);
        const size_t write_len = buf.size();
        const size_t calc_len = src.SerializedSize();
        EXPECT_EQ(write_len, calc_len);
        PackFileMeta dst;
        const char* p = buf.data();
        dst.Deserialize(p);
        EXPECT_EQ(write_len, static_cast<size_t>(p - buf.data()));
        ASSERT_THAT(dst.dir_id_to_child_dir_ids.find(dir1_id)->second, ElementsAre(dir2_id));
        EXPECT_EQ(dst.dir_id_to_child_dir_ids.find(dir2_id)->second.size(), 0);
        ASSERT_THAT(dst.dir_id_to_child_file_ids.find(dir1_id)->second, ElementsAre(file1_id, file2_id));
        ASSERT_THAT(dst.dir_id_to_child_file_sizes.find(dir2_id)->second, ElementsAre(file3_id, file4_id));

        AssertEntryName(dst.dir_entry_records, dst.dir_name_data, dir1_id, dir1, "dir");
        AssertEntryName(dst.dir_entry_records, dst.dir_name_data, dir2_id, dir2, "dir");

        AssertEntryName(dst.file_entry_records, dst.file_name_data, file1_id, file1, "file");
        AssertEntryName(dst.file_entry_records, dst.file_name_data, file2_id, file2, "file");
        AssertEntryName(dst.file_entry_records, dst.file_name_data, file3_id, file3, "file");
        AssertEntryName(dst.file_entry_records, dst.file_name_data, file4_id, file4, "file");
    }


    TEST(FileBlobMetaTest, ClearResetAllData)
    {
        PackFileMeta meta;
        meta.dir_name_data = "/test/folder";
        meta.dir_entry_records.put(1, EntryRecord{});
        meta.dir_id_to_child_dir_ids.put(1, {2, 3});
        meta.Clear();
        EXPECT_TRUE(meta.dir_name_data.empty());
        EXPECT_TRUE(meta.file_name_data.empty());
        EXPECT_TRUE(meta.dir_entry_records.empty());
        EXPECT_TRUE(meta.file_entry_records.empty());
        EXPECT_TRUE(meta.dir_id_to_child_dir_ids.empty());
        EXPECT_TRUE(meta.dir_id_to_child_file_ids.empty());
        EXPECT_TRUE(meta.dir_id_to_child_file_sizes.empty());
    }


    class FileBlobHandlePresetFileTest : public test::PresetFileFixture
    {
    };


    TEST_F(FileBlobHandlePresetFileTest, ScanDirectory)
    {
        PackFileHandle file_blob_handle;
        Status status = file_blob_handle.ScanDirectory(source_dir_);
        ASSERT_TRUE(status);
        auto file_ids = file_blob_handle.GetDirIdToChildFileIds();
        size_t file_cnt = 0;
        size_t file_size = 0;
        for (const auto& dir_file_map : file_ids)
        {
            auto sub_file_ids = dir_file_map.second;
            file_cnt += sub_file_ids.size();
            auto file_sizes = file_blob_handle.GetFileSizeByDirId(dir_file_map.first);
            file_size += std::accumulate(file_sizes.begin(), file_sizes.end(), size_t{0});
            auto dir_name_status = file_blob_handle.GetDirPathByDirId(dir_file_map.first);
            ASSERT_TRUE(dir_name_status);
            auto dir_name = std::string(dir_name_status.value());
            ASSERT_TRUE(test::ContainsElement(preset_dirs, dir_name)) << " should contain " << dir_name;
            auto dir_id_status = file_blob_handle.GetDirIdByDirPath(dir_name);
            ASSERT_TRUE(dir_id_status);
            ASSERT_EQ(dir_id_status.value(), dir_file_map.first);
            for (const entry_id_t& sub_file_id : sub_file_ids)
            {
                auto file_name_status = file_blob_handle.GetFileNameByFileId(sub_file_id);
                ASSERT_TRUE(file_name_status);
                auto file_name = std::string(file_name_status.value());
                ASSERT_TRUE(test::ContainsElement(preset_key_words, file_name)) << " should contain " << file_name;
            }
        }

        EXPECT_EQ(file_cnt, generator_file_cnt);
        EXPECT_EQ(file_size, generator_file_size);
        EXPECT_EQ(file_cnt, file_blob_handle.TotalFileCnt());
    }

    TEST_F(FileBlobHandlePresetFileTest, SerializeDeserialize)
    {
        PackFileHandle pack_file_handle;
        Status status = pack_file_handle.ScanDirectory(source_dir_);
        ASSERT_TRUE(status);
        std::string buf;
        pack_file_handle.SerializePackFileMeta(&buf);
        ASSERT_EQ(pack_file_handle.PackFileMetaSerializedSize(), buf.size());
        PackFileHandle dst;
        const char* p = buf.data();
        status = dst.DeserializePackFileMeta(p);
        ASSERT_TRUE(status);
        ASSERT_EQ(pack_file_handle.PackFileMetaSerializedSize(), dst.PackFileMetaSerializedSize());
    }

    TEST_F(FileBlobHandlePresetFileTest, RenderDirListingTree)
    {
        PackFileHandle pack_file_handle;
        Status status = pack_file_handle.ScanDirectory(source_dir_);
        ASSERT_TRUE(status);
        std::ostringstream oss;

        status = pack_file_handle.RenderDirListing(oss);
        ASSERT_TRUE(status);
        std::string tree_text = oss.str();
        ASSERT_TRUE(test::StringContainsAll(tree_text, preset_key_words));
    }

    TEST_F(FileBlobHandlePresetFileTest, RenderDirListingList)
    {
        PackFileHandle pack_file_handle;
        Status status = pack_file_handle.ScanDirectory(source_dir_);
        ASSERT_TRUE(status);
        std::ostringstream oss;

        status = pack_file_handle.RenderDirListing(oss, kRootDirId, "", OutputMode::LIST);
        ASSERT_TRUE(status);

        std::string list_text = oss.str();
        ASSERT_TRUE(test::StringContainsAll(list_text, file_paths));
        auto output_lines = test::SplitView(list_text, '\n');
        // Note: LIST mode adds '\n' after every entry. Trailing newline makes line count larger than actual file count.
        ASSERT_EQ(output_lines.size() - 1, file_paths.size());
        for (const auto& output_line : output_lines)
        {
            if (output_line.empty())
            {
                continue;
            }
            auto row = test::SplitView(output_line, ' ');
            ASSERT_GT(row.size(), kListColumnSize);
            auto file_path = std::string(row[row.size() - 1]);
            ASSERT_TRUE(test::ContainsElement(file_paths, file_path));
        }
    }

    TEST_F(FileBlobHandlePresetFileTest, RenderDirListingTsv)
    {
        PackFileHandle pack_file_handle;
        Status status = pack_file_handle.ScanDirectory(source_dir_);
        ASSERT_TRUE(status);
        std::ostringstream oss;

        status = pack_file_handle.RenderDirListing(oss, kRootDirId, "", OutputMode::TSV);
        ASSERT_TRUE(status);
        std::string tsv_text = oss.str();
        auto output_lines = test::SplitView(tsv_text, '\n');
        // Note: TSV mode adds '\n' after every entry. Trailing newline makes line count larger than actual file count.
        ASSERT_EQ(output_lines.size() - 1, file_paths.size());
        for (const auto& output_line : output_lines)
        {
            if (output_line.empty())
            {
                continue;
            }
            auto row = test::SplitView(output_line, '\t');
            ASSERT_EQ(row.size(), kTsvColumnSize);
            uint32_t permissions = std::stoll(std::string(row[3]));
            std::string file_owner_name = std::string(row[4]);
            std::string file_group_name = std::string(row[5]);
            auto file_modify_time = std::stoll(std::string(row[6]));
            auto file_size = std::stoll(std::string(row[8]));
            auto dir_path = std::string(row[kTsvColumnSize - 2]);
            auto file_name = std::string(row[kTsvColumnSize - 1]);
            auto file_path = std::format("{}/{}",dir_path,file_name);

            ASSERT_EQ(fs::file_size(file_path), file_size);

            const auto ftime = fs::last_write_time(file_path);
            const auto sys_tp = std::chrono::file_clock::to_sys(ftime);
            auto modify_time = std::chrono::duration_cast<std::chrono::milliseconds>(sys_tp.time_since_epoch()).count();
            ASSERT_EQ(file_modify_time, modify_time);
            struct stat file_stat{};
            auto s = stat(file_path.c_str(), &file_stat);
            ASSERT_EQ(s, 0);
            ASSERT_EQ(permissions, static_cast<uint32_t>(file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)));

            char owner_name[64];
            char group_name[64];
            passwd* pw = getpwuid(file_stat.st_uid);
            ASSERT_NE(pw, nullptr);
            std::snprintf(owner_name, std::size(owner_name), "%s", pw->pw_name);
            EXPECT_EQ(std::string(owner_name), file_owner_name);

            group* gr = getgrgid(file_stat.st_gid);
            ASSERT_NE(gr, nullptr);
            std::snprintf(group_name, std::size(group_name), "%s", gr->gr_name);
            EXPECT_EQ(std::string(group_name), file_group_name);
        }
    }


} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}