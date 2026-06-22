# CSPack
A single-header library defining the csp format: a packed, memory-mappable container of blobs.

A csp file is a 32-byte header followed by a 16-byte-aligned content region. One header is the single source of truth
for the layout and is shared by both sides: the writer packs blobs into a file at build-time, and the mapper memory-maps
that file at run-time and points spans straight at the bytes, with no parsing or allocation per blob.

## Features
- Single header, depending only on the C++20 standard library and the OS mapping API.
- One definition of the format, shared by the writer and the mapper so they cannot drift.
- Memory-mapping; reading pages in on demand stays zero-copy, so mapping is cheap regardless of file size.
- A CRC32 fingerprint guards against corruption and an FNV-1a signature ties a file to the build that expects it.

## Requirements
- Windows or Linux OS.
- A C++20 compiler.

## Usage
### Writer (Build-Time)
Append blobs in order, then serialise. `table` records the `(offset, size)` each blob landed at, and `signature()` ties
the file to this build; the build emits both so the consumer can resolve its regions and validate the file.
```cpp
csp::pack pack;
pack.append(first_bytes);
pack.append(second_bytes);
csp::write(pack, "Data.csp");
```

### Mapper (Run-Time)
`mount()` maps the file from `directory`, validates its header against the build-time signature, and exposes the mapped
region through `csp::current`. The mapping is read-only; point spans straight at their regions against `current.base()`,
or copy out anything you need to keep. The mapped bytes stay valid until exit.
```cpp
// At startup, before anything reads the file.
csp::mount(directory, "Data.csp", 6125984697962060194ull);

const std::span<const unsigned char> first{csp::current.base() + 32, 2036};
const std::span<const unsigned char> second{csp::current.base() + 2080, 500000};
```
