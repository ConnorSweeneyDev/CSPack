// CSP 1.0.0

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>

  #include <fileapi.h>
  #include <handleapi.h>
  #include <memoryapi.h>
  #include <minwindef.h>
  #include <winnt.h>

  #ifdef near
    #undef near
  #endif
  #ifdef far
    #undef far
  #endif
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

#if defined(_MSC_VER) && defined(_M_X64)
  #include <intrin.h>
#elif defined(__x86_64__) && defined(__clang__)
  #include <cpuid.h>
  #include <nmmintrin.h>
#elif defined(__x86_64__) && defined(__GNUC__)
  #include <cpuid.h>
  #pragma GCC push_options
  #pragma GCC target("sse4.2")
  #include <nmmintrin.h>
  #pragma GCC pop_options
#endif

#if (defined(_MSC_VER) && defined(_M_X64)) || (defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)))
  #define CSP_HARDWARE_CRC32C
#endif

namespace csp
{
  namespace detail
  {
    inline constexpr std::uint32_t crc32c_poly{0x82F63B78u};

    constexpr std::array<std::array<std::uint32_t, 256>, 8> make_crc32c_table()
    {
      std::array<std::array<std::uint32_t, 256>, 8> table{};
      for (std::uint32_t entry{}; entry < 256; ++entry)
      {
        std::uint32_t value{entry};
        for (int bit{}; bit < 8; ++bit) value = (value & 1u) ? (crc32c_poly ^ (value >> 1)) : (value >> 1);
        table[0][entry] = value;
      }
      for (std::uint32_t entry{}; entry < 256; ++entry)
        for (std::size_t slice{1}; slice < 8; ++slice)
          table[slice][entry] = (table[slice - 1][entry] >> 8) ^ table[0][table[slice - 1][entry] & 0xFFu];
      return table;
    }
    inline constexpr std::array<std::array<std::uint32_t, 256>, 8> crc32c_table{make_crc32c_table()};

    inline std::uint32_t crc32c_software(std::uint32_t crc, const unsigned char *bytes, const std::size_t size)
    {
      std::size_t index{};
      for (; index + 8 <= size; index += 8)
      {
        std::uint64_t word{};
        std::memcpy(&word, bytes + index, sizeof(word));
        word ^= crc;
        crc = crc32c_table[7][word & 0xFFu] ^ crc32c_table[6][(word >> 8) & 0xFFu] ^
              crc32c_table[5][(word >> 16) & 0xFFu] ^ crc32c_table[4][(word >> 24) & 0xFFu] ^
              crc32c_table[3][(word >> 32) & 0xFFu] ^ crc32c_table[2][(word >> 40) & 0xFFu] ^
              crc32c_table[1][(word >> 48) & 0xFFu] ^ crc32c_table[0][(word >> 56) & 0xFFu];
      }
      for (; index < size; ++index) crc = crc32c_table[0][(crc ^ bytes[index]) & 0xFFu] ^ (crc >> 8);
      return crc;
    }

#ifdef CSP_HARDWARE_CRC32C
    inline constexpr std::size_t long_block{8192};
    inline constexpr std::size_t short_block{256};

    constexpr std::uint32_t multmodp(std::uint32_t a, std::uint32_t b)
    {
      std::uint32_t product{};
      for (;;)
      {
        if (a & 0x80000000u)
        {
          product ^= b;
          if ((a & 0x7FFFFFFFu) == 0) break;
        }
        a <<= 1;
        b = (b & 1u) ? (b >> 1) ^ crc32c_poly : (b >> 1);
      }
      return product;
    }
    constexpr std::array<std::array<std::uint32_t, 256>, 4> make_crc32c_zeros(std::size_t length)
    {
      std::uint32_t op{0x80000000u};
      std::uint32_t sq{op >> 4};
      while (length)
      {
        sq = multmodp(sq, sq);
        if (length & 1) op = multmodp(sq, op);
        length >>= 1;
      }
      std::array<std::array<std::uint32_t, 256>, 4> table{};
      for (std::uint32_t n{}; n < 256; ++n)
      {
        table[0][n] = multmodp(op, n);
        table[1][n] = multmodp(op, n << 8);
        table[2][n] = multmodp(op, n << 16);
        table[3][n] = multmodp(op, n << 24);
      }
      return table;
    }
    inline constexpr std::array<std::array<std::uint32_t, 256>, 4> crc32c_zeros_long{make_crc32c_zeros(long_block)};
    inline constexpr std::array<std::array<std::uint32_t, 256>, 4> crc32c_zeros_short{make_crc32c_zeros(short_block)};
    inline std::uint32_t crc32c_shift(const std::array<std::array<std::uint32_t, 256>, 4> &zeros,
                                      const std::uint32_t crc)
    {
      return zeros[0][crc & 0xFFu] ^ zeros[1][(crc >> 8) & 0xFFu] ^ zeros[2][(crc >> 16) & 0xFFu] ^ zeros[3][crc >> 24];
    }

  #if defined(__GNUC__) || defined(__clang__)
    __attribute__((target("sse4.2")))
  #endif
    inline std::uint32_t crc32c_hardware(const std::uint32_t crc, const unsigned char *bytes, const std::size_t size)
    {
      std::uint64_t crc0{crc};
      std::size_t index{};

      while (size - index >= long_block * 3)
      {
        std::uint64_t crc1{}, crc2{};
        for (std::size_t step{}; step < long_block; step += 8)
        {
          std::uint64_t a{}, b{}, c{};
          std::memcpy(&a, bytes + index + step, sizeof(a));
          std::memcpy(&b, bytes + index + step + long_block, sizeof(b));
          std::memcpy(&c, bytes + index + step + long_block * 2, sizeof(c));
          crc0 = _mm_crc32_u64(crc0, a);
          crc1 = _mm_crc32_u64(crc1, b);
          crc2 = _mm_crc32_u64(crc2, c);
        }
        crc0 = crc32c_shift(crc32c_zeros_long, static_cast<std::uint32_t>(crc0)) ^ crc1;
        crc0 = crc32c_shift(crc32c_zeros_long, static_cast<std::uint32_t>(crc0)) ^ crc2;
        index += long_block * 3;
      }
      while (size - index >= short_block * 3)
      {
        std::uint64_t crc1{}, crc2{};
        for (std::size_t step{}; step < short_block; step += 8)
        {
          std::uint64_t a{}, b{}, c{};
          std::memcpy(&a, bytes + index + step, sizeof(a));
          std::memcpy(&b, bytes + index + step + short_block, sizeof(b));
          std::memcpy(&c, bytes + index + step + short_block * 2, sizeof(c));
          crc0 = _mm_crc32_u64(crc0, a);
          crc1 = _mm_crc32_u64(crc1, b);
          crc2 = _mm_crc32_u64(crc2, c);
        }
        crc0 = crc32c_shift(crc32c_zeros_short, static_cast<std::uint32_t>(crc0)) ^ crc1;
        crc0 = crc32c_shift(crc32c_zeros_short, static_cast<std::uint32_t>(crc0)) ^ crc2;
        index += short_block * 3;
      }

      for (; index + 8 <= size; index += 8)
      {
        std::uint64_t chunk{};
        std::memcpy(&chunk, bytes + index, sizeof(chunk));
        crc0 = _mm_crc32_u64(crc0, chunk);
      }
      std::uint32_t crc32{static_cast<std::uint32_t>(crc0)};
      for (; index < size; ++index) crc32 = _mm_crc32_u8(crc32, bytes[index]);
      return crc32;
    }
    inline bool crc32c_supported()
    {
  #if defined(_MSC_VER)
      int registers[4]{};
      __cpuid(registers, 1);
      return (registers[2] & (1 << 20)) != 0;
  #else
      unsigned int eax{}, ebx{}, ecx{}, edx{};
      if (!__get_cpuid(1u, &eax, &ebx, &ecx, &edx)) return false;
      return (ecx & (1u << 20)) != 0;
  #endif
    }
#endif
  }

  /*
   Format
   A csp file is a 32-byte header, a 16-byte-aligned content region, then a directory of one record per blob. The writer
   (build-time) and mapper (run-time) agree on this layout. Each blob carries its own CRC-32C, so the mapper verifies
   blobs lazily on first access rather than scanning the whole file at start-up.
  */
  inline constexpr char magic[4]{'C', 'S', 'P', '0'};
  inline constexpr std::uint32_t version{2};
  struct header
  {
    char magic[4];
    std::uint32_t version;
    std::uint32_t flags;
    std::uint32_t count;
    std::uint64_t signature;
    std::uint64_t size;
  };
  struct entry
  {
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t fingerprint;
  };
  static_assert(sizeof(header) == 32);

  inline std::uint64_t signature(const void *data, const std::size_t size)
  {
    const auto *bytes{static_cast<const unsigned char *>(data)};
    std::uint64_t hash{14695981039346656037ull};
    for (std::size_t index{}; index < size; ++index)
    {
      hash ^= bytes[index];
      hash *= 1099511628211ull;
    }
    return hash;
  }
  inline std::uint32_t fingerprint(const void *data, const std::size_t size)
  {
    const auto *bytes{static_cast<const unsigned char *>(data)};
    std::uint32_t crc{0xFFFFFFFFu};
#ifdef CSP_HARDWARE_CRC32C
    if (detail::crc32c_supported())
      crc = detail::crc32c_hardware(crc, bytes, size);
    else
#endif
      crc = detail::crc32c_software(crc, bytes, size);
    return ~crc;
  }

  /*
   Writer (Build-Time)
   Pack is a memory-mappable container of blobs. Append blobs in order, then write() serialises the header followed by
   the 16-byte-aligned content region. `table` records the (offset, size) each blob landed at so the build can emit
   matching patches; it is not serialised.
  */
  struct pack
  {
    std::vector<std::byte> content{};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> table{};

    void append(const std::vector<std::byte> &blob)
    {
      while (content.size() % 16 != 0) content.push_back(std::byte{});
      table.emplace_back(static_cast<std::uint64_t>(sizeof(header) + content.size()),
                         static_cast<std::uint64_t>(blob.size()));
      content.insert(content.end(), blob.begin(), blob.end());
    }
    std::uint64_t signature() const { return csp::signature(content.data(), content.size()); }
  };

  inline void write(const pack &container, const std::filesystem::path &file)
  {
    header head{};
    std::memcpy(head.magic, magic, sizeof(magic));
    head.version = version;
    head.flags = 0;
    head.count = static_cast<std::uint32_t>(container.table.size());
    head.signature = container.signature();
    head.size = static_cast<std::uint64_t>(container.content.size());

    std::vector<std::byte> bytes(sizeof(header));
    std::memcpy(bytes.data(), &head, sizeof(header));
    bytes.insert(bytes.end(), container.content.begin(), container.content.end());
    for (const auto &[offset, size] : container.table)
    {
      entry record{};
      record.offset = offset;
      record.size = size;
      record.fingerprint = csp::fingerprint(container.content.data() + (offset - sizeof(header)), size);
      const auto *raw{reinterpret_cast<const std::byte *>(&record)};
      bytes.insert(bytes.end(), raw, raw + sizeof(record));
    }

    std::ofstream output_file(file, std::ios::binary);
    if (!output_file.is_open()) throw std::runtime_error("Failed to open file: " + file.string());
    if (!output_file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error("Failed to write file: " + file.string());
  }

  /*
   Mapper (Run-Time)
   mount() maps a file at a known path, validates its header against the build-time signature, and registers it by name
   in `mappings`; it returns the mapping so the caller can resolve its own spans lazily against base(). Several packs
   may be mounted at once. The free verify() locates the owning pack for a pointer, so consumers verify by pointer
   without tracking which pack a blob came from. Mappings are read-only; consumers treat them as immutable bytes and
   copy anything they need to keep.
  */
  class mapping
  {
  public:
    mapping() = default;
    ~mapping()
    {
      delete[] entries;
      delete[] validated;
      if (!data) return;
#ifdef _WIN32
      UnmapViewOfFile(data);
#else
      munmap(const_cast<unsigned char *>(data), length);
#endif
    }
    mapping(const mapping &) = delete;
    mapping &operator=(const mapping &) = delete;

    void load_directory(const header &head)
    {
      delete[] entries;
      delete[] validated;
      count = head.count;
      entries = new entry[count];
      validated = new std::atomic<bool>[count]();
      const unsigned char *table{data + sizeof(header) + head.size};
      const std::uint64_t content_end{sizeof(header) + head.size};
      for (std::size_t index{}; index < count; ++index)
      {
        std::memcpy(&entries[index], table + index * sizeof(entry), sizeof(entry));
        const entry &record{entries[index]};
        if (record.offset < sizeof(header) || record.size > head.size || record.offset > content_end - record.size)
          throw std::runtime_error("Csp directory entry is out of bounds");
      }
    }

    void open(const std::filesystem::path &file)
    {
#ifdef _WIN32
      void *handle{CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
      if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Failed to open csp file '" + file.string() + "'");
      LARGE_INTEGER bytes{};
      if (!GetFileSizeEx(handle, &bytes))
      {
        CloseHandle(handle);
        throw std::runtime_error("Failed to size csp file '" + file.string() + "'");
      }
      void *map{CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr)};
      if (!map)
      {
        CloseHandle(handle);
        throw std::runtime_error("Failed to map csp file '" + file.string() + "'");
      }
      void *view{MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0)};
      CloseHandle(map);
      CloseHandle(handle);
      if (!view) throw std::runtime_error("Failed to view csp file '" + file.string() + "'");
      data = static_cast<const unsigned char *>(view);
      length = static_cast<std::size_t>(bytes.QuadPart);
#else
      const int descriptor{::open(file.c_str(), O_RDONLY)};
      if (descriptor < 0) throw std::runtime_error("Failed to open csp file '" + file.string() + "'");
      struct stat status{};
      if (fstat(descriptor, &status) != 0)
      {
        ::close(descriptor);
        throw std::runtime_error("Failed to size csp file '" + file.string() + "'");
      }
      void *address{mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0)};
      ::close(descriptor);
      if (address == MAP_FAILED) throw std::runtime_error("Failed to map csp file '" + file.string() + "'");
      data = static_cast<const unsigned char *>(address);
      length = static_cast<std::size_t>(status.st_size);
#endif
    }

    const unsigned char *verify(const std::uint64_t offset, const std::uint64_t size)
    {
      const entry *begin{entries};
      const auto after{std::upper_bound(begin, begin + count, offset, [](std::uint64_t value, const entry &record)
                                        { return value < record.offset; })};
      if (after == begin) throw std::runtime_error("Csp span is outside any blob");
      const entry *found{after - 1};
      if (size > found->size || offset - found->offset > found->size - size)
        throw std::runtime_error("Csp span is outside any blob");
      const std::size_t index{static_cast<std::size_t>(found - begin)};
      if (!validated[index].load(std::memory_order_acquire))
      {
        if (csp::fingerprint(data + found->offset, found->size) != found->fingerprint)
          throw std::runtime_error("Csp blob is corrupted");
        validated[index].store(true, std::memory_order_release);
      }
      return data + offset;
    }
    const unsigned char *verify(const unsigned char *pointer, const std::uint64_t size)
    {
      if (pointer < data || pointer >= data + length) return pointer;
      return verify(static_cast<std::uint64_t>(pointer - data), size);
    }

    const unsigned char *base() const { return data; }
    std::size_t size() const { return length; }

  private:
    const unsigned char *data{};
    std::size_t length{};
    entry *entries{};
    std::atomic<bool> *validated{};
    std::size_t count{};
  };

  inline std::unordered_map<std::string, mapping> &mappings()
  {
    static std::unordered_map<std::string, mapping> instance{};
    return instance;
  }

  inline mapping &mount(const std::filesystem::path &directory, const std::string &name, const std::uint64_t signature)
  {
    const std::filesystem::path file{directory / name};
    mappings().erase(name);
    mapping &map{mappings()[name]};
    map.open(file);

    const unsigned char *base{map.base()};
    if (map.size() < sizeof(header)) throw std::runtime_error("Csp file '" + file.string() + "' is truncated");
    const header &head{*reinterpret_cast<const header *>(base)};
    if (std::memcmp(head.magic, magic, sizeof(magic)) != 0)
      throw std::runtime_error("Csp file '" + file.string() + "' is not a csp file");
    if (head.version != version)
      throw std::runtime_error("Csp file '" + file.string() + "' has an unsupported version");
    if (head.signature != signature)
      throw std::runtime_error("Csp file '" + file.string() + "' does not match this build");
    if (map.size() != sizeof(header) + head.size + static_cast<std::size_t>(head.count) * sizeof(entry))
      throw std::runtime_error("Csp file '" + file.string() + "' has an unexpected size");
    map.load_directory(head);
    return map;
  }

  inline const unsigned char *verify(const unsigned char *pointer, const std::uint64_t size)
  {
    for (auto &item : mappings())
    {
      mapping &map{item.second};
      const unsigned char *base{map.base()};
      if (base && pointer >= base && pointer < base + map.size()) return map.verify(pointer, size);
    }
    return pointer;
  }

  inline mapping &mounted(const std::string &name)
  {
    const auto found{mappings().find(name)};
    if (found == mappings().end()) throw std::runtime_error("Csp pack '" + name + "' is not mounted");
    return found->second;
  }

  inline void unmount(const std::string &name) { mappings().erase(name); }
}
