#include "security/dpapi_secret_store.hpp"

#include <cstdint>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace mikudesk::security {

#if defined(_WIN32)
namespace {

app::Result<std::string> EncodeBase64(const std::vector<std::uint8_t>& binary) {
  if (binary.empty()) {
    return std::string();
  }

  DWORD required_size = 0;
  if (!CryptBinaryToStringA(binary.data(), static_cast<DWORD>(binary.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &required_size)) {
    return std::unexpected(app::AppError::kSecretEncryptFailed);
  }

  std::string encoded(required_size, '\0');
  if (!CryptBinaryToStringA(binary.data(), static_cast<DWORD>(binary.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(),
                            &required_size)) {
    return std::unexpected(app::AppError::kSecretEncryptFailed);
  }

  if (!encoded.empty() && encoded.back() == '\0') {
    encoded.pop_back();
  }
  return encoded;
}

app::Result<std::vector<std::uint8_t>> DecodeBase64(std::string_view encoded) {
  if (encoded.empty()) {
    return std::vector<std::uint8_t>();
  }

  DWORD required_size = 0;
  if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64,
                            nullptr, &required_size, nullptr, nullptr)) {
    return std::unexpected(app::AppError::kSecretDecryptFailed);
  }

  std::vector<std::uint8_t> binary(required_size);
  if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64,
                            binary.data(), &required_size, nullptr, nullptr)) {
    return std::unexpected(app::AppError::kSecretDecryptFailed);
  }

  return binary;
}

}  // namespace
#endif

app::Result<std::string> DpapiSecretStore::Encrypt(std::string_view plain_text) {
#if defined(_WIN32)
  if (plain_text.empty()) {
    return std::string();
  }

  DATA_BLOB input_blob{};
  input_blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain_text.data()));
  input_blob.cbData = static_cast<DWORD>(plain_text.size());

  DATA_BLOB output_blob{};
  if (!CryptProtectData(&input_blob, L"MikuDeskSecret", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output_blob)) {
    return std::unexpected(app::AppError::kSecretEncryptFailed);
  }

  std::vector<std::uint8_t> binary(output_blob.pbData, output_blob.pbData + output_blob.cbData);
  LocalFree(output_blob.pbData);
  return EncodeBase64(binary);
#else
  (void)plain_text;
  return std::unexpected(app::AppError::kSecretEncryptFailed);
#endif
}

app::Result<std::string> DpapiSecretStore::Decrypt(std::string_view cipher_text) {
#if defined(_WIN32)
  if (cipher_text.empty()) {
    return std::string();
  }

  auto decoded = DecodeBase64(cipher_text);
  if (!decoded.has_value()) {
    return std::unexpected(decoded.error());
  }

  DATA_BLOB input_blob{};
  input_blob.pbData = decoded->data();
  input_blob.cbData = static_cast<DWORD>(decoded->size());

  DATA_BLOB output_blob{};
  if (!CryptUnprotectData(&input_blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &output_blob)) {
    return std::unexpected(app::AppError::kSecretDecryptFailed);
  }

  std::string plain_text(reinterpret_cast<char*>(output_blob.pbData), output_blob.cbData);
  LocalFree(output_blob.pbData);
  return plain_text;
#else
  (void)cipher_text;
  return std::unexpected(app::AppError::kSecretDecryptFailed);
#endif
}

}  // namespace mikudesk::security
