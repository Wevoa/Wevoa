#pragma once

#include <stdexcept>
#include <string>

#include "utils/source.h"

namespace wevoaweb {

class WevoaError : public std::runtime_error {
  public:
    WevoaError(const std::string& message, const SourceSpan& span)
        : std::runtime_error(message), span_(span) {}

    const SourceSpan& span() const noexcept {
        return span_;
    }

  private:
    SourceSpan span_;
};

class LexError : public WevoaError {
  public:
    using WevoaError::WevoaError;
};

class ParseError : public WevoaError {
  public:
    using WevoaError::WevoaError;
};

class RuntimeError : public WevoaError {
  public:
    using WevoaError::WevoaError;
};

}  // namespace wevoaweb
