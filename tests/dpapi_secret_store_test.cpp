#include <iostream>
#include <string>
#include <string_view>

#include "security/dpapi_secret_store.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

}  // namespace

int main() {
  mikudesk::security::DpapiSecretStore secret_store;

  {
    constexpr std::string_view kSource = "hello mikudesk";
    auto encrypted = secret_store.Encrypt(kSource);
    if (!Expect(encrypted.has_value(), "Encrypt should succeed")) {
      return 1;
    }
    if (!Expect(!encrypted->empty(), "Encrypted text should not be empty")) {
      return 1;
    }

    auto decrypted = secret_store.Decrypt(*encrypted);
    if (!Expect(decrypted.has_value(), "Decrypt should succeed")) {
      return 1;
    }
    if (!Expect(*decrypted == kSource, "Roundtrip value mismatch")) {
      return 1;
    }
  }

  {
    auto encrypted = secret_store.Encrypt("");
    if (!Expect(encrypted.has_value(), "Encrypt empty should succeed")) {
      return 1;
    }
    if (!Expect(encrypted->empty(), "Encrypted empty should stay empty")) {
      return 1;
    }

    auto decrypted = secret_store.Decrypt(*encrypted);
    if (!Expect(decrypted.has_value(), "Decrypt empty should succeed")) {
      return 1;
    }
    if (!Expect(decrypted->empty(), "Decrypted empty should stay empty")) {
      return 1;
    }
  }

  {
    auto invalid = secret_store.Decrypt("%%%not_base64%%%");
    if (!Expect(!invalid.has_value(), "Invalid base64 should fail")) {
      return 1;
    }
  }

  return 0;
}
