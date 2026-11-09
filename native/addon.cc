#include <node_api.h>

#include <dlfcn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void* OpenLibrary(napi_env env, const char* soname) {
  void* handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* error = dlerror();
    napi_throw_error(env, nullptr,
                     error != nullptr ? error : "Could not load shared library");
  }
  return handle;
}

void* CryptoHandle(napi_env env) {
  static void* handle = OpenLibrary(env, "libcrypto.so.3");
  return handle;
}

void* ZlibHandle(napi_env env) {
  static void* handle = OpenLibrary(env, "libz.so.1");
  return handle;
}

template <typename Function>
Function LoadSymbol(napi_env env, void* handle, const char* name) {
  dlerror();
  void* symbol = dlsym(handle, name);
  const char* error = dlerror();
  if (error != nullptr) {
    napi_throw_error(env, nullptr, error);
    return nullptr;
  }
  return reinterpret_cast<Function>(symbol);
}

bool Check(napi_env env, napi_status status, const char* message) {
  if (status == napi_ok) {
    return true;
  }

  const napi_extended_error_info* info = nullptr;
  napi_get_last_error_info(env, &info);
  napi_throw_error(env, nullptr,
                   info != nullptr && info->error_message != nullptr
                       ? info->error_message
                       : message);
  return false;
}

bool ReadInput(napi_env env, napi_callback_info callback_info,
               std::vector<std::uint8_t>* input) {
  std::array<napi_value, 1> arguments{};
  std::size_t argument_count = arguments.size();
  if (!Check(env,
             napi_get_cb_info(env, callback_info, &argument_count,
                              arguments.data(), nullptr, nullptr),
             "Could not read function arguments")) {
    return false;
  }

  if (argument_count != 1) {
    napi_throw_type_error(env, nullptr,
                          "Expected exactly one Buffer or string argument");
    return false;
  }

  bool is_buffer = false;
  if (!Check(env, napi_is_buffer(env, arguments[0], &is_buffer),
             "Could not inspect input")) {
    return false;
  }

  if (is_buffer) {
    void* data = nullptr;
    std::size_t length = 0;
    if (!Check(env, napi_get_buffer_info(env, arguments[0], &data, &length),
               "Could not read Buffer input")) {
      return false;
    }

    if (length == 0) {
      input->clear();
    } else {
      const auto* bytes = static_cast<const std::uint8_t*>(data);
      input->assign(bytes, bytes + length);
    }
    return true;
  }

  napi_valuetype type = napi_undefined;
  if (!Check(env, napi_typeof(env, arguments[0], &type),
             "Could not inspect input type")) {
    return false;
  }
  if (type != napi_string) {
    napi_throw_type_error(env, nullptr, "Input must be a Buffer or string");
    return false;
  }

  std::size_t length = 0;
  if (!Check(env,
             napi_get_value_string_utf8(env, arguments[0], nullptr, 0, &length),
             "Could not measure string input")) {
    return false;
  }

  std::vector<char> text(length + 1);
  std::size_t copied = 0;
  if (!Check(env,
             napi_get_value_string_utf8(env, arguments[0], text.data(),
                                        text.size(), &copied),
             "Could not read string input")) {
    return false;
  }
  input->assign(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(copied));
  return true;
}

napi_value OpenSslVersion(napi_env env, napi_callback_info) {
  void* handle = CryptoHandle(env);
  if (handle == nullptr) {
    return nullptr;
  }
  auto version = LoadSymbol<decltype(&OpenSSL_version)>(
      env, handle, "OpenSSL_version");
  if (version == nullptr) {
    return nullptr;
  }

  napi_value result = nullptr;
  if (!Check(env,
             napi_create_string_utf8(env, version(OPENSSL_VERSION),
                                     NAPI_AUTO_LENGTH, &result),
             "Could not create OpenSSL version string")) {
    return nullptr;
  }
  return result;
}

napi_value ZlibVersion(napi_env env, napi_callback_info) {
  void* handle = ZlibHandle(env);
  if (handle == nullptr) {
    return nullptr;
  }
  auto version =
      LoadSymbol<decltype(&zlibVersion)>(env, handle, "zlibVersion");
  if (version == nullptr) {
    return nullptr;
  }

  napi_value result = nullptr;
  if (!Check(env,
             napi_create_string_utf8(env, version(), NAPI_AUTO_LENGTH,
                                     &result),
             "Could not create zlib version string")) {
    return nullptr;
  }
  return result;
}

napi_value Sha256(napi_env env, napi_callback_info callback_info) {
  std::vector<std::uint8_t> input;
  if (!ReadInput(env, callback_info, &input)) {
    return nullptr;
  }

  void* handle = CryptoHandle(env);
  if (handle == nullptr) {
    return nullptr;
  }
  auto digest_function =
      LoadSymbol<decltype(&EVP_Digest)>(env, handle, "EVP_Digest");
  auto sha256_function =
      LoadSymbol<decltype(&EVP_sha256)>(env, handle, "EVP_sha256");
  if (digest_function == nullptr || sha256_function == nullptr) {
    return nullptr;
  }

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_length = 0;
  if (digest_function(input.data(), input.size(), digest.data(), &digest_length,
                      sha256_function(), nullptr) != 1) {
    napi_throw_error(env, nullptr, "OpenSSL SHA-256 operation failed");
    return nullptr;
  }

  constexpr char hex_digits[] = "0123456789abcdef";
  std::string hex(digest_length * 2, '\0');
  for (unsigned int index = 0; index < digest_length; ++index) {
    hex[index * 2] = hex_digits[digest[index] >> 4];
    hex[index * 2 + 1] = hex_digits[digest[index] & 0x0f];
  }

  napi_value result = nullptr;
  if (!Check(env,
             napi_create_string_utf8(env, hex.c_str(), hex.size(), &result),
             "Could not create SHA-256 result")) {
    return nullptr;
  }
  return result;
}

constexpr std::size_t kLengthPrefixBytes = 4;
constexpr std::uint32_t kMaxArchiveBytes = 1024 * 1024;

bool CompressBytes(napi_env env, const std::vector<std::uint8_t>& input,
                   std::vector<std::uint8_t>* output) {
  if (input.size() > kMaxArchiveBytes) {
    napi_throw_range_error(env, nullptr, "Input exceeds 1 MiB");
    return false;
  }

  void* handle = ZlibHandle(env);
  if (handle == nullptr) {
    return false;
  }
  auto compress_bound =
      LoadSymbol<decltype(&compressBound)>(env, handle, "compressBound");
  auto compress_function =
      LoadSymbol<decltype(&compress2)>(env, handle, "compress2");
  if (compress_bound == nullptr || compress_function == nullptr) {
    return false;
  }

  uLongf compressed_length = compress_bound(input.size());
  output->resize(kLengthPrefixBytes + compressed_length);
  const auto original_length = static_cast<std::uint32_t>(input.size());
  for (std::size_t index = 0; index < kLengthPrefixBytes; ++index) {
    (*output)[index] =
        static_cast<std::uint8_t>(original_length >> (index * 8));
  }

  const Bytef* source = input.empty() ? reinterpret_cast<const Bytef*>("")
                                      : input.data();
  int status = compress_function(output->data() + kLengthPrefixBytes,
                                 &compressed_length, source, input.size(),
                                 Z_DEFAULT_COMPRESSION);
  if (status != Z_OK) {
    napi_throw_error(env, nullptr, "zlib compression failed");
    return false;
  }
  output->resize(kLengthPrefixBytes + compressed_length);
  return true;
}

bool DecompressBytes(napi_env env, const std::vector<std::uint8_t>& input,
                     std::vector<std::uint8_t>* output) {
  if (input.size() <= kLengthPrefixBytes) {
    napi_throw_error(env, nullptr, "Archived data is missing its size header");
    return false;
  }

  std::uint32_t expected_length = 0;
  for (std::size_t index = 0; index < kLengthPrefixBytes; ++index) {
    expected_length |= static_cast<std::uint32_t>(input[index]) << (index * 8);
  }
  if (expected_length > kMaxArchiveBytes) {
    napi_throw_range_error(env, nullptr, "Archived data exceeds 1 MiB");
    return false;
  }

  void* handle = ZlibHandle(env);
  if (handle == nullptr) {
    return false;
  }
  auto uncompress_function =
      LoadSymbol<decltype(&uncompress)>(env, handle, "uncompress");
  if (uncompress_function == nullptr) {
    return false;
  }

  uLongf output_length = std::max<std::uint32_t>(expected_length, 1);
  std::vector<Bytef> decoded(output_length);
  int status = uncompress_function(
      decoded.data(), &output_length, input.data() + kLengthPrefixBytes,
      input.size() - kLengthPrefixBytes);
  if (status != Z_OK || output_length != expected_length) {
    napi_throw_error(env, nullptr, "zlib decompression failed");
    return false;
  }
  output->assign(decoded.begin(),
                 decoded.begin() + static_cast<std::ptrdiff_t>(output_length));
  return true;
}

napi_value CreateBuffer(napi_env env, const std::vector<std::uint8_t>& bytes) {
  napi_value result = nullptr;
  if (!Check(env,
             napi_create_buffer_copy(env, bytes.size(), bytes.data(), nullptr,
                                     &result),
             "Could not create output Buffer")) {
    return nullptr;
  }
  return result;
}

napi_value Compress(napi_env env, napi_callback_info callback_info) {
  std::vector<std::uint8_t> input;
  if (!ReadInput(env, callback_info, &input)) {
    return nullptr;
  }
  std::vector<std::uint8_t> compressed;
  return CompressBytes(env, input, &compressed) ? CreateBuffer(env, compressed)
                                                : nullptr;
}

napi_value Decompress(napi_env env, napi_callback_info callback_info) {
  std::vector<std::uint8_t> input;
  if (!ReadInput(env, callback_info, &input)) {
    return nullptr;
  }
  std::vector<std::uint8_t> decompressed;
  return DecompressBytes(env, input, &decompressed)
             ? CreateBuffer(env, decompressed)
             : nullptr;
}

napi_value CompressRoundTrip(napi_env env, napi_callback_info callback_info) {
  std::vector<std::uint8_t> input;
  if (!ReadInput(env, callback_info, &input)) {
    return nullptr;
  }
  std::vector<std::uint8_t> compressed;
  std::vector<std::uint8_t> decompressed;
  if (!CompressBytes(env, input, &compressed) ||
      !DecompressBytes(env, compressed, &decompressed)) {
    return nullptr;
  }

  return CreateBuffer(env, decompressed);
}

napi_value Initialize(napi_env env, napi_value exports) {
  const std::array<napi_property_descriptor, 6> properties{{
      {"opensslVersion", nullptr, OpenSslVersion, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"zlibVersion", nullptr, ZlibVersion, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"sha256", nullptr, Sha256, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"compress", nullptr, Compress, nullptr, nullptr, nullptr, napi_default,
       nullptr},
      {"decompress", nullptr, Decompress, nullptr, nullptr, nullptr,
       napi_default, nullptr},
      {"compressRoundTrip", nullptr, CompressRoundTrip, nullptr, nullptr,
       nullptr, napi_default, nullptr},
  }};

  if (!Check(env,
             napi_define_properties(env, exports, properties.size(),
                                    properties.data()),
             "Could not export native functions")) {
    return nullptr;
  }
  return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Initialize)
