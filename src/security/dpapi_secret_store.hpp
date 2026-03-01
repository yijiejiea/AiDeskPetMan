#pragma once

#include "security/secret_store.hpp"

namespace mikudesk::security {

class DpapiSecretStore : public ISecretStore {
 public:
  app::Result<std::string> Encrypt(std::string_view plain_text) override;
  app::Result<std::string> Decrypt(std::string_view cipher_text) override;
};

}  // namespace mikudesk::security
