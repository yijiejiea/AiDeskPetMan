#pragma once

#include <string>
#include <string_view>

#include "app/app_error.hpp"

namespace mikudesk::security {

class ISecretStore {
 public:
  virtual ~ISecretStore() = default;

  virtual app::Result<std::string> Encrypt(std::string_view plain_text) = 0;
  virtual app::Result<std::string> Decrypt(std::string_view cipher_text) = 0;
};

}  // namespace mikudesk::security
