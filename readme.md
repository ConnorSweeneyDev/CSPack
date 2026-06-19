# CSPack
A single-header library defining the csp format: a packed, memory-mappable container of blobs.

A csp file is a 32-byte header followed by a 16-byte-aligned content region. One header is the single source of truth
for the layout and is shared by both sides: the writer packs blobs into a file at build-time, and the mapper memory-maps
that file at run-time and points spans straight at the bytes, with no parsing or allocation per blob.

## Features
- Single header, depending only on the C++20 standard library and the OS mapping API.
- One definition of the format, shared by the writer and the mapper so they cannot drift.
- Memory-mapped reads: the OS pages content in on demand, so mapping is cheap regardless of file size.
- A CRC32 fingerprint guards against corruption and an FNV-1a signature ties a file to the build that expects it.

## Requirements
- Windows or Linux OS.
- A C++20 compiler.

## Usage
### Writer (Build-Time)
Append blobs in order, then serialise. `table` records the `(offset, size)` each blob landed at so the build can emit
matching patches for the mapper.
```cpp
csp::pack pack;
pack.append(first_bytes);
pack.append(second_bytes);
csp::write(pack, "Data.csp");
```

### Mapper (Run-Time)
Declare a manifest of patches; each points a span at a region of the file. Constructing the manifest registers it, then
`mount()` maps the file, validates the header, and fills in every span. `directory` is the folder the file lives in.
```cpp
std::span<const unsigned char> first;
std::span<const unsigned char> second;

namespace csp
{
  constexpr std::uint64_t expected{/* The generated signature for the build */};
  const std::array<patch, 2> patches{{
    {&first, 32, 2036},
    {&second, 2080, 500000},
  }};
  const manifest instance{"Data.csp", expected, patches};
}

// At startup, before anything reads the spans to make them point straight at their regions of the file.
csp::mount(directory);
```
