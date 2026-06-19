#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
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

  inline std::uint32_t fingerprint(const void *data, std::size_t size)
  {
    const auto *bytes{static_cast<const unsigned char *>(data)};
    std::uint32_t crc{0xFFFFFFFFu};
    for (std::size_t index{}; index < size; ++index)
    {
      crc ^= bytes[index];
      for (int bit{}; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
  }
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
   The application declares a manifest of patches: each patch points a span at a region of the mapped file. mount() maps
   the file, validates the header, and fills in every span. Constructing a manifest registers it for the next mount().
  */
  struct patch
  {
    std::span<const unsigned char> *target;
    std::uint64_t offset;
    std::uint64_t size;
  };
  struct manifest
  {
    manifest(const char *path, std::uint64_t signature, std::span<const patch> patches);
    const char *path;
    std::uint64_t signature;
    std::span<const patch> patches;
  } inline const *registered{};
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

  inline manifest::manifest(const char *path_, std::uint64_t signature_, std::span<const patch> patches_)
    : path{path_}, signature{signature_}, patches{patches_}
  { registered = this; }

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

  inline void mount(const std::filesystem::path &directory)
  {
    if (!registered) return;

    const std::filesystem::path file{directory / registered->path};
    current.open(file);

    const unsigned char *base{current.base()};
    if (current.size() < sizeof(header)) throw std::runtime_error("Csp file '" + file.string() + "' is truncated");
    const header &head{*reinterpret_cast<const header *>(base)};
    if (std::memcmp(head.magic, magic, sizeof(magic)) != 0)
      throw std::runtime_error("Csp file '" + file.string() + "' is not a csp file");
    if (head.version != version)
      throw std::runtime_error("Csp file '" + file.string() + "' has an unsupported version");
    if (head.signature != registered->signature)
      throw std::runtime_error("Csp file '" + file.string() + "' does not match this build");
    if (current.size() != sizeof(header) + head.size)
      throw std::runtime_error("Csp file '" + file.string() + "' has an unexpected size");
    if (csp::fingerprint(base + sizeof(header), head.size) != head.fingerprint)
      throw std::runtime_error("Csp file '" + file.string() + "' is corrupted");

    for (const auto &entry : registered->patches)
      *entry.target = std::span<const unsigned char>{base + entry.offset, entry.size};
  }
}
