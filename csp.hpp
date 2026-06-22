// CSP 1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
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
  #include <winnt.h>
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
  /*
   Format
   A csp file is a 32-byte header followed by a 16-byte-aligned content region. The writer (build-time) and mapper
   (run-time) agree on this layout and the two algorithms below.
  */
  inline constexpr char magic[4]{'C', 'S', 'P', '0'};
  inline constexpr std::uint32_t version{1};
  struct header
  {
    char magic[4];
    std::uint32_t version;
    std::uint32_t flags;
    std::uint32_t fingerprint;
    std::uint64_t signature;
    std::uint64_t size;
  };
  static_assert(sizeof(header) == 32);

  inline std::uint64_t signature(const void *data, std::size_t size)
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

  namespace detail
  {
    constexpr std::array<std::uint32_t, 256> make_crc32c_table()
    {
      std::array<std::uint32_t, 256> table{};
      for (std::uint32_t entry{}; entry < 256; ++entry)
      {
        std::uint32_t value{entry};
        for (int bit{}; bit < 8; ++bit) value = (value & 1u) ? (0x82F63B78u ^ (value >> 1)) : (value >> 1);
        table[entry] = value;
      }
      return table;
    }
    inline constexpr std::array<std::uint32_t, 256> crc32c_table{make_crc32c_table()};

    inline std::uint32_t crc32c_software(std::uint32_t crc, const unsigned char *bytes, std::size_t size)
    {
      for (std::size_t index{}; index < size; ++index) crc = crc32c_table[(crc ^ bytes[index]) & 0xFFu] ^ (crc >> 8);
      return crc;
    }
#ifdef CSP_HARDWARE_CRC32C
  #if defined(__GNUC__) || defined(__clang__)
    __attribute__((target("sse4.2")))
  #endif
    inline std::uint32_t crc32c_hardware(std::uint32_t crc, const unsigned char *bytes, std::size_t size)
    {
      std::uint64_t accumulator{crc};
      std::size_t index{};
      for (; index + 8 <= size; index += 8)
      {
        std::uint64_t chunk{};
        std::memcpy(&chunk, bytes + index, sizeof(chunk));
        accumulator = _mm_crc32_u64(accumulator, chunk);
      }
      std::uint32_t crc32{static_cast<std::uint32_t>(accumulator)};
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
  inline std::uint32_t fingerprint(const void *data, std::size_t size)
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
    std::uint32_t fingerprint() const { return csp::fingerprint(content.data(), content.size()); }
  };

  inline void write(const pack &container, const std::filesystem::path &file)
  {
    header head{};
    std::memcpy(head.magic, magic, sizeof(magic));
    head.version = version;
    head.flags = 0;
    head.fingerprint = container.fingerprint();
    head.signature = container.signature();
    head.size = static_cast<std::uint64_t>(container.content.size());

    std::vector<std::byte> bytes(sizeof(header));
    std::memcpy(bytes.data(), &head, sizeof(header));
    bytes.insert(bytes.end(), container.content.begin(), container.content.end());

    std::ofstream output_file(file, std::ios::binary);
    if (!output_file.is_open()) throw std::runtime_error("Failed to open file: " + file.string());
    if (!output_file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error("Failed to write file: " + file.string());
  }

  /*
   Mapper (Run-Time)
   mount() maps the file at a known path, validates its header against the build-time signature, and exposes the mapped
   region through `current`. Consumers resolve their own spans lazily against `current.base()` rather than being
   patched. The mapping is read-only; consumers treat it as immutable bytes and copy anything they need to keep.
  */
  class mapping
  {
  public:
    mapping() = default;
    ~mapping();
    mapping(const mapping &) = delete;
    mapping &operator=(const mapping &) = delete;

    void open(const std::filesystem::path &file);
    const unsigned char *base() const { return data; }
    std::size_t size() const { return length; }

  private:
    const unsigned char *data{};
    std::size_t length{};
  } inline current{};

  inline mapping::~mapping()
  {
    if (!data) return;
#ifdef _WIN32
    UnmapViewOfFile(data);
#else
    munmap(const_cast<unsigned char *>(data), length);
#endif
  }

  inline void mapping::open(const std::filesystem::path &file)
  {
#ifdef _WIN32
    void *handle{
      CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
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

  inline void mount(const std::filesystem::path &directory, const char *name, std::uint64_t signature)
  {
    const std::filesystem::path file{directory / name};
    current.open(file);

    const unsigned char *base{current.base()};
    if (current.size() < sizeof(header)) throw std::runtime_error("Csp file '" + file.string() + "' is truncated");
    const header &head{*reinterpret_cast<const header *>(base)};
    if (std::memcmp(head.magic, magic, sizeof(magic)) != 0)
      throw std::runtime_error("Csp file '" + file.string() + "' is not a csp file");
    if (head.version != version)
      throw std::runtime_error("Csp file '" + file.string() + "' has an unsupported version");
    if (head.signature != signature)
      throw std::runtime_error("Csp file '" + file.string() + "' does not match this build");
    if (current.size() != sizeof(header) + head.size)
      throw std::runtime_error("Csp file '" + file.string() + "' has an unexpected size");
    if (csp::fingerprint(base + sizeof(header), head.size) != head.fingerprint)
      throw std::runtime_error("Csp file '" + file.string() + "' is corrupted");
  }
}
